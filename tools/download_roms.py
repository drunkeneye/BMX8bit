#!/usr/bin/env python3
"""Download missing firmware ROMs for BMX staging (stdlib only).

VICE ROMs come from https://www.zimmers.net/anonftp/pub/cbm/firmware/
(crawled + name-normalized, so layout changes don't break the mapping),
except the SuperCPU scpu64 binary, which is intentionally never fetched.
Atari ROMs come from https://github.com/ascrnet/FW-Altirra (Automatic/).

Layout (repo-relative):
  roms/<machine-lower>/...   persistent cache (c64, c128, vic20, plus4,
                             pet, drives, scpu64, a800)
  third_party/vice-3.10/data/<MACHINE>/...
                             build/stage input (populated by copying new
                             VICE downloads out of the cache)
  roms/a800/...              staged 1:1 to SD /roms (atari800 matches by
                             CRC32/size, so upstream names are kept)

Files already present (size > 0) are never re-downloaded. New downloads
are length-checked when the server reports one. Failures warn loudly but
never fail the build: staging still emits MISSING-ROMS.txt as before.
"""
import html.parser
import json
import os
import re
import socket
import sys
import time
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(REPO, "roms")
VICE_DATA = os.path.join(REPO, "third_party", "vice-3.10", "data")

ZIMMERS = "https://www.zimmers.net/anonftp/pub/cbm/firmware/"
FW_API = ("https://api.github.com/repos/ascrnet/FW-Altirra/"
          "contents/Automatic")
FW_RAW = ("https://raw.githubusercontent.com/ascrnet/FW-Altirra/"
          "main/Automatic/")

# (cache dir, vice-data dir or None, filename as staging needs it).
# Mirrors the features=["roms"] entries in sd-layout.toml.
VICE_ROMS = [
    ("c64", "C64", "basic-901226-01.bin"),
    ("c64", "C64", "kernal-901227-03.bin"),
    ("c64", "C64", "chargen-901225-01.bin"),
    ("c128", "C128", "basiclo-318018-04.bin"),
    ("c128", "C128", "basichi-318019-04.bin"),
    ("c128", "C128", "basic64-901226-01.bin"),
    ("c128", "C128", "kernal64-901227-03.bin"),
    ("c128", "C128", "kernal-318020-05.bin"),
    ("c128", "C128", "chargen-390059-01.bin"),
    ("vic20", "VIC20", "basic-901486-01.bin"),
    ("vic20", "VIC20", "chargen-901460-03.bin"),
    ("vic20", "VIC20", "kernal.901486-06.bin"),
    ("vic20", "VIC20", "kernal.901486-07.bin"),
    ("plus4", "PLUS4", "basic-318006-01.bin"),
    ("plus4", "PLUS4", "3plus1-317053-01.bin"),
    ("plus4", "PLUS4", "3plus1-317054-01.bin"),
    ("plus4", "PLUS4", "kernal-318004-05.bin"),
    ("plus4", "PLUS4", "kernal-318005-05.bin"),
    ("pet", "PET", "basic-4.901465-23-20-21.bin"),
    ("pet", "PET", "characters-2.901447-10.bin"),
    ("pet", "PET", "kernal-4.901465-22.bin"),
    ("pet", "PET", "edit-4-40-n-50Hz.901498-01.bin"),
    ("pet", "PET", "edit-4-80-b-50Hz.901474-04_.bin"),
    ("drives", "DRIVES", "dos1001-901887+8-01.bin"),
    ("drives", "DRIVES", "dos1540-325302+3-01.bin"),
    ("drives", "DRIVES", "dos1541-325302-01+901229-05.bin"),
    ("drives", "DRIVES", "dos1541ii-251968-03.bin"),
    ("drives", "DRIVES", "dos1551-318008-01.bin"),
    ("drives", "DRIVES", "dos1570-315090-01.bin"),
    ("drives", "DRIVES", "dos1571-310654-05.bin"),
    ("drives", "DRIVES", "dos1571cr-318047-01.bin"),
    ("drives", "DRIVES", "dos1581-318045-02.bin"),
    ("drives", "DRIVES", "dos2031-901484-03+05.bin"),
    ("drives", "DRIVES", "dos2040-901468-06+07.bin"),
    ("drives", "DRIVES", "dos3040-901468-11-13.bin"),
    ("drives", "DRIVES", "dos4040-901468-14-16.bin"),
    ("drives", "DRIVES", "dos9000-300516+7-revC.bin"),
    ("scpu64", "SCPU64", "chargen-901225-01.bin"),
    # NOTE: roms/SCPU64/scpu64 (the SuperCPU binary) is deliberately
    # absent: not downloadable from zimmers, stays in MISSING-ROMS.txt.
]

UA = {"User-Agent": "BMX-rom-fetch/1.0 (staging helper)"}

# Expected uncompressed sizes, taken from the known-good local set. Every
# download (single, alias or assembled) must match, otherwise it is dropped.
# Same MOS part number + same size = same ROM.
EXPECTED_SIZES = {
    "basic-901226-01.bin": 8192,
    "kernal-901227-03.bin": 8192,
    "chargen-901225-01.bin": 4096,
    "basiclo-318018-04.bin": 16384,
    "basichi-318019-04.bin": 16384,
    "basic64-901226-01.bin": 8192,
    "kernal64-901227-03.bin": 8192,
    "kernal-318020-05.bin": 16384,
    "chargen-390059-01.bin": 8192,
    "basic-901486-01.bin": 8192,
    "chargen-901460-03.bin": 4096,
    "kernal.901486-06.bin": 8192,
    "kernal.901486-07.bin": 8192,
    "basic-318006-01.bin": 16384,
    "kernal-318004-05.bin": 16384,
    "kernal-318005-05.bin": 16384,
    "basic-4.901465-23-20-21.bin": 12288,
    "characters-2.901447-10.bin": 2048,
    "kernal-4.901465-22.bin": 4096,
    "edit-4-40-n-50Hz.901498-01.bin": 2048,
    "edit-4-80-b-50Hz.901474-04_.bin": 2048,
    "dos1540-325302+3-01.bin": 16384,
    "dos1541-325302-01+901229-05.bin": 16384,
    "dos1541ii-251968-03.bin": 16384,
    "dos1551-318008-01.bin": 16384,
    "dos1570-315090-01.bin": 32768,
    "dos1571-310654-05.bin": 32768,
    "dos1571cr-318047-01.bin": 32768,
    "dos1581-318045-02.bin": 32768,
}

# Required name -> upstream basename. Only same-MOS-part renames
# (chargen/characters, basic64, kernal64, dos-prefix, -rom infix).
ALIASES = {
    "chargen-901225-01.bin": "characters.901225-01.bin",
    "basic64-901226-01.bin": "basic.901226-01.bin",
    "kernal64-901227-03.bin": "kernal.901227-03.bin",
    "basiclo-318018-04.bin": "basic-4000.318018-04.bin",
    "basichi-318019-04.bin": "basic-8000.318019-04.bin",
    "kernal-318020-05.bin": "kernal.318020-05.bin",
    "chargen-390059-01.bin": "characters.390059-01.bin",
    "chargen-901460-03.bin": "characters.901460-03.bin",
    "kernal-318004-05.bin": "kernal.318004-05.bin",
    "kernal-318005-05.bin": "kernal.318005-05.bin",
    "basic-318006-01.bin": "basic.318006-01.bin",
    "dos1541ii-251968-03.bin": "1541-II.251968-03.bin",
    "dos1551-318008-01.bin": "1551.318008-01.bin",
    "dos1570-315090-01.bin": "1570-rom.315090-01.bin",
    "dos1571-310654-05.bin": "1571-rom.310654-05.bin",
    "dos1571cr-318047-01.bin": "1571cr-rom.318047-01.bin",
    "dos1581-318045-02.bin": "1581-rom.318045-02.bin",
}

# Required name -> ordered part basenames, concatenated. Only ascending
# address order with exact part numbers (no guessing across revisions).
ASSEMBLIES = {
    "basic-4.901465-23-20-21.bin": [
        "basic-4-b000.901465-23.bin",
        "basic-4-c000.901465-20.bin",
        "basic-4-d000.901465-21.bin",
    ],
    "dos1540-325302+3-01.bin": [
        "1540-c000.325302-01.bin",
        "1540-e000.325303-01.bin",
    ],
    "dos1541-325302-01+901229-05.bin": [
        "1541-c000.325302-01.bin",
        "1541-e000.901229-05.bin",
    ],
}
# Deliberately NOT covered (ambiguous or incomplete upstream, keep local):
# 3plus1-*, edit-4-80-b-*, dos1001, dos2031, dos2040, dos3040, dos4040,
# dos9000, and the scpu64 binary (policy exclusion).


def fetch(url, timeout=60):
    """GET with retries for transient network/DNS blips (home-router DNS
    forwarders hand out EAI_AGAIN under load; the Pi here resolves via one).
    Retries URLError/timeout only; HTTP errors (404 etc.) fail at once."""
    req = urllib.request.Request(url, headers=UA)
    last = None
    for attempt, wait in ((1, 0), (2, 2), (3, 5)):
        if wait:
            time.sleep(wait)
        try:
            return urllib.request.urlopen(req, timeout=timeout)
        except urllib.error.HTTPError:
            raise
        except Exception as e:
            last = e
            print("  net try %d %s (%s)" % (attempt, url.split("?")[0], e))
    raise last


def dns_preflight():
    """Resolve both ROM hosts up front. Returns True if usable; prints a
    pinpointed diagnostic otherwise (this is what an offline run looks
    like, instead of 40 identical tracebacks)."""
    old_timeout = socket.getdefaulttimeout()
    socket.setdefaulttimeout(10)
    try:
        ok = True
        for host in ("www.zimmers.net", "api.github.com"):
            try:
                socket.getaddrinfo(host, 443)
                print("  dns ok: %s" % host)
            except Exception as e:
                print("  DNS FAIL: %s (%s)" % (host, e))
                print("    check link (/etc/resolv.conf, router, WiFi)"
                      " and retry")
                ok = False
        return ok
    finally:
        socket.setdefaulttimeout(old_timeout)


def norm(name):
    """Normalize ROM filenames across naming styles (dots vs dashes)."""
    base = name.rsplit("/", 1)[-1].lower()
    return "".join(c for c in base if c not in "._- ")


class LinkParser(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            for k, v in attrs:
                if k == "href" and v and not v.startswith(
                        ("?", "#", "mailto:")):
                    self.links.append(v)


def crawl_zimmers():
    """Return {normalized-name: url} for firmware files.

    Single fetch of the ALLFILES.html index (full paths included);
    falls back to a bounded recursive crawl if the index is missing.
    """
    try:
        with fetch(ZIMMERS + "ALLFILES.html", timeout=60) as r:
            body = r.read().decode("utf-8", "replace")
        found = {}
        for m in re.finditer(r'href="([^"]+)"', body):
            href = m.group(1)
            if href.startswith(("?", "#", "mailto:", "../")):
                continue
            if href.endswith("/"):
                continue
            low = href.lower()
            if not low.endswith((".bin", ".rom")):
                continue
            full = (href if "://" in href
                    else urllib.parse.urljoin(ZIMMERS, href))
            if not full.startswith("https://www.zimmers.net/"):
                continue
            key = norm(full)
            if key not in found:
                found[key] = full
        if found:
            return found
    except Exception as e:
        print("  ALLFILES.html unavailable (%s), crawling" % e)
    return crawl_zimmers_recursive()


def crawl_zimmers_recursive():
    """Bounded recursive crawl (fallback only)."""
    seen_dirs = set()
    found = {}
    stack = [(ZIMMERS, 0)]
    pages = 0
    while stack and pages < 600:
        url, depth = stack.pop()
        if url in seen_dirs or depth > 4:
            continue
        seen_dirs.add(url)
        try:
            with fetch(url, timeout=30) as r:
                if "html" not in r.headers.get("Content-Type", ""):
                    continue
                body = r.read().decode("utf-8", "replace")
        except Exception as e:
            print("  crawl skip %s (%s)" % (url, e))
            continue
        pages += 1
        p = LinkParser()
        try:
            p.feed(body)
        except Exception:
            continue
        for href in p.links:
            if href in ("../", "/"):
                continue
            full = (href if "://" in href
                    else urllib.parse.urljoin(url, href))
            if not full.startswith(ZIMMERS):
                continue  # stay inside the firmware tree
            low = full.lower()
            # Directory pages link as subdir/index.html (no trailing slash).
            if href.endswith("/") or low.endswith("index.html") or (
                    "." not in href.rsplit("/", 1)[-1]):
                stack.append((full, depth + 1))
            elif low.endswith((".bin", ".rom", ".rom1", ".rom2")):
                key = norm(full)
                if key not in found:
                    found[key] = full
    return found


def fetch_bytes(url):
    """Return body bytes, or None on any failure/length mismatch."""
    # GitHub's API sometimes returns download_urls with raw spaces; some
    # hosts need '+' and parens escaped too. Unquote first (idempotent for
    # already-encoded URLs), then quote the path.
    url = urllib.parse.quote(urllib.parse.unquote(url), safe=":/")
    try:
        with fetch(url) as r:
            expected = r.headers.get("Content-Length")
            data = r.read()
        if expected is not None and len(data) != int(expected):
            print("  SHORT %s (%d/%s bytes)" % (url, len(data), expected))
            return None
        if not data:
            print("  EMPTY %s" % url)
            return None
        return data
    except Exception as e:
        print("  FAIL %s (%s)" % (url, e))
        return None


def present(path):
    """True if path exists with nonzero size (never re-download those)."""
    try:
        return os.path.getsize(path) > 0
    except OSError:
        return False


def write_file(dest, data):
    tmp = dest + ".part"
    try:
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, dest)
        return True
    except OSError as e:
        print("  WRITE-FAIL %s (%s)" % (dest, e))
        try:
            os.remove(tmp)
        except OSError:
            pass
        return False


def resolve_vice(name, index, by_base):
    """Return file bytes for a required ROM, or None.

    Order: exact normalized match, curated alias (same MOS part),
    mechanical assembly (ascending addresses). Every result must match
    EXPECTED_SIZES.
    """
    want = EXPECTED_SIZES.get(name)
    if index is not None:
        url = index.get(norm(name))
        if url is not None:
            data = fetch_bytes(url)
            if data is not None and (want is None or len(data) == want):
                return data
            print("  SIZE-MISMATCH %s" % name)
            return None
    if index is not None and name in ALIASES:
        url = by_base.get(ALIASES[name].lower())
        if url is not None:
            data = fetch_bytes(url)
            if data is not None and (want is None or len(data) == want):
                return data
            print("  SIZE-MISMATCH %s" % name)
            return None
    if index is not None and name in ASSEMBLIES:
        parts = []
        for base in ASSEMBLIES[name]:
            url = by_base.get(base.lower())
            if url is None:
                return None
            data = fetch_bytes(url)
            if data is None:
                return None
            parts.append(data)
        blob = b"".join(parts)
        if want is not None and len(blob) != want:
            print("  SIZE-MISMATCH %s" % name)
            return None
        return blob
    return None


def fetch_vice(index):
    by_base = {}
    if index is not None:
        for key, url in index.items():
            by_base.setdefault(url.rsplit("/", 1)[-1].lower(), url)
    got, skipped, failed = 0, 0, []
    for cache_dir, data_dir, name in VICE_ROMS:
        cache_path = os.path.join(CACHE, cache_dir, name)
        data_path = os.path.join(VICE_DATA, data_dir, name)
        if present(data_path):
            if not present(cache_path):
                # Mirror into the cache so roms/ reflects reality.
                try:
                    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
                    with open(data_path, "rb") as f:
                        blob = f.read()
                    with open(cache_path, "wb") as f:
                        f.write(blob)
                except OSError as e:
                    print("  cache mirror failed %s (%s)" % (name, e))
            skipped += 1
            continue
        if present(cache_path):
            # Cache has it (e.g. manual drop): promote into build input.
            try:
                os.makedirs(os.path.dirname(data_path), exist_ok=True)
                with open(cache_path, "rb") as f:
                    blob = f.read()
                with open(data_path, "wb") as f:
                    f.write(blob)
                print("  promote %s" % name)
                got += 1
            except OSError as e:
                print("  promote failed %s (%s)" % (name, e))
                failed.append(name)
            continue
        if index is None:
            failed.append(name)
            continue
        print("  get %s" % name)
        data = resolve_vice(name, index, by_base)
        if data is None:
            print("  NOT-FOUND-UPSTREAM %s (keeping local/MISSING)" % name)
            failed.append(name)
            continue
        try:
            os.makedirs(os.path.dirname(cache_path), exist_ok=True)
            os.makedirs(os.path.dirname(data_path), exist_ok=True)
            if not write_file(cache_path, data):
                failed.append(name)
                continue
            with open(cache_path, "rb") as f:
                blob = f.read()
            if not write_file(data_path, blob):
                failed.append(name)
                continue
            got += 1
        except OSError as e:
            print("  store failed %s (%s)" % (name, e))
            failed.append(name)
    return got, skipped, failed


def fw_file_list():
    try:
        with fetch(FW_API) as r:
            items = json.load(r)
        out = []
        for it in items:
            if it.get("type") == "file" and it.get("name"):
                out.append((it["name"], it.get("download_url", "")))
        return out
    except Exception as e:
        print("  FW-Altirra list failed (%s)" % e)
        return None


def fetch_atari(files):
    dest_dir = os.path.join(CACHE, "a800")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for name, url in files:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            skipped += 1
            continue
        if not url:
            failed.append(name)
            continue
        print("  get %s" % name)
        data = fetch_bytes(url)
        if data is not None and write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    return got, skipped, failed


def main():
    only = set(a[7:] for a in sys.argv[1:] if a.startswith("--only="))
    do_vice = not only or "vice" in only or "c64" in only
    do_atari = not only or "atari" in only or "a800" in only

    # Staging copies repo/roms/a800 1:1 (both.tree requires the source to
    # exist, even when empty), so the cache dirs always exist afterwards
    # — even fully offline, when there is nothing to put in them yet.
    for cache_dir, _, _ in VICE_ROMS:
        os.makedirs(os.path.join(CACHE, cache_dir), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "a800"), exist_ok=True)

    total_failed = []
    net_ok = dns_preflight()
    if do_vice:
        print("VICE ROMs (zimmers.net, scpu64 binary excluded):")
        if net_ok:
            try:
                index = crawl_zimmers()
                print("  index: %d files" % len(index))
            except Exception as e:
                print("  index failed (%s); only local files apply" % e)
                index = None
        else:
            print("  offline; only local files apply")
            index = None
        got, skipped, failed = fetch_vice(index)
        print("  vice: %d fetched, %d present, %d failed" %
              (got, skipped, len(failed)))
        total_failed += ["vice:" + n for n in failed]
    if do_atari:
        print("Atari ROMs (FW-Altirra Automatic/):")
        files = fw_file_list() if net_ok else None
        if files is None:
            if net_ok:
                print("  atari: list unavailable, skipping")
            else:
                print("  atari: offline, skipping")
        else:
            got, skipped, failed = fetch_atari(files)
            print("  atari: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["a800:" + n for n in failed]
    if total_failed:
        print("STILL MISSING (%d):" % len(total_failed))
        for n in total_failed:
            print("  - %s" % n)
        print("(staging will list these in MISSING-ROMS.txt as before)")
    else:
        print("All ROMs present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
