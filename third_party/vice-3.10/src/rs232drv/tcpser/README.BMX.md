Imported tcpser modem-core subset
=================================

Source: https://github.com/go4retro/tcpser

License: GPL-2.0-or-later, as stated in the upstream README.

Imported files:

- modem_core.c / modem_core.h
- getcmd.c / getcmd.h
- dce.h
- line.h
- nvt.h
- phone_book.c / phone_book.h

BMX does not build the original tcpser POSIX application. It reuses the Hayes
modem command parser/core and supplies a bare-metal adapter for the DCE and TCP
line I/O.
