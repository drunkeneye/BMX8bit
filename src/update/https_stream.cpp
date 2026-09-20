#include "update/https_stream.h"

#include "update/update_foreground_progress.h"

#include <stdio.h>
#include <string.h>

namespace bmx {
namespace update {

namespace {

bool IsRedirectStatus(unsigned status) {
    return status == 301U || status == 302U || status == 303U ||
           status == 307U || status == 308U;
}

UpdateForegroundPhase ForegroundPhase(HttpsRequestKind kind) {
    switch (kind) {
    case HttpsRequestKind::GitHubApi:
        return UpdateForegroundPhase::Discovery;
    case HttpsRequestKind::ReleaseMetadata:
        return UpdateForegroundPhase::Manifest;
    case HttpsRequestKind::ReleaseZip:
        return UpdateForegroundPhase::Zip;
    case HttpsRequestKind::GitHubDeviceFlow:
        return UpdateForegroundPhase::Discovery;
    }
    return UpdateForegroundPhase::Discovery;
}

bool NetworkCheckpoint(UpdateForegroundProgress *progress,
                       HttpsRequestKind kind, uint64_t completed,
                       uint64_t total) {
    return progress == 0 || progress->Checkpoint(
        ForegroundPhase(kind), completed, total,
        UpdateForegroundCancelBehavior::CancelAtCheckpoint);
}

bool SendAll(SecureByteStream *stream, const uint8_t *data, size_t size,
             HttpsRequestKind kind, UpdateForegroundProgress *progress,
             bool *canceled) {
    if (canceled != 0) *canceled = false;
    size_t sent = 0U;
    while (sent < size) {
        if (!NetworkCheckpoint(progress, kind, sent, size)) {
            if (canceled != 0) *canceled = true;
            return false;
        }
        const int result = stream->Send(data + sent, size - sent);
        if (result <= 0 || static_cast<size_t>(result) > size - sent) {
            return false;
        }
        sent += static_cast<size_t>(result);
        if (!NetworkCheckpoint(progress, kind, sent, size)) {
            if (canceled != 0) *canceled = true;
            return false;
        }
    }
    return true;
}

const char *AcceptHeader(HttpsRequestKind kind) {
    return kind == HttpsRequestKind::GitHubApi
               ? "application/vnd.github+json"
               : kind == HttpsRequestKind::GitHubDeviceFlow
                   ? "application/json"
               : "application/octet-stream";
}

bool SafeHeaderValue(const char *value, size_t maximum) {
    if (value == 0) return true;
    size_t size = 0U;
    while (size <= maximum && value[size] != '\0') {
        const unsigned char c = static_cast<unsigned char>(value[size]);
        if (c < 0x21U || c > 0x7eU) return false;
        ++size;
    }
    return size != 0U && size <= maximum;
}

bool InitialPurposeAllowed(UpdateUrlPurpose purpose) {
    return purpose == UpdateUrlPurpose::Discovery ||
           purpose == UpdateUrlPurpose::ReleaseAsset ||
           purpose == UpdateUrlPurpose::PreparedDraftDiscovery ||
           purpose == UpdateUrlPurpose::AuthenticatedReleaseAsset ||
           purpose == UpdateUrlPurpose::GitHubDeviceAuthorization;
}

void SetString(char *output, size_t output_size, const char *value) {
    if (output_size == 0U) return;
    const size_t value_size = strlen(value);
    const size_t copy_size = value_size < output_size - 1U
                                 ? value_size
                                 : output_size - 1U;
    memcpy(output, value, copy_size);
    output[copy_size] = '\0';
}

}  // namespace

HttpsGetResult HttpsRequest(
    const char *url, UpdateUrlPurpose initial_purpose,
    HttpsRequestKind request_kind, const HttpsRequestOptions &options,
    uint64_t maximum_body_bytes, HttpBodySink *sink,
    SecureStreamFactory *factory, HttpsGetWorkspace *workspace,
    UpdateForegroundProgress *foreground_progress) {
    HttpsGetResult result;
    memset(&result, 0, sizeof(result));
    result.status = HttpsGetStatus::InvalidArgument;
    result.url_status = UrlPolicyStatus::InvalidArgument;
    result.header_status = HttpParseStatus::InvalidArgument;
    result.body_status = HttpBodyStatus::InvalidArgument;
    if (url == 0 || sink == 0 || factory == 0 || workspace == 0 ||
        maximum_body_bytes == 0U || !InitialPurposeAllowed(initial_purpose) ||
        options.method == 0 ||
        (strcmp(options.method, "GET") != 0 &&
         strcmp(options.method, "POST") != 0) ||
        !SafeHeaderValue(options.accept, 128U) ||
        !SafeHeaderValue(options.content_type, 128U) ||
        !SafeHeaderValue(options.bearer_token, 512U) ||
        (strcmp(options.method, "GET") == 0 && options.body.size != 0U) ||
        (strcmp(options.method, "POST") == 0 &&
         (options.body.data == 0 || options.body.size == 0U ||
          options.content_type == 0))) {
        return result;
    }
    if (!NetworkCheckpoint(foreground_progress, request_kind, 0U, 0U)) {
        result.status = HttpsGetStatus::Canceled;
        return result;
    }

    if (strlen(url) >= sizeof(workspace->current_url)) {
        result.status = HttpsGetStatus::UrlRejected;
        result.url_status = UrlPolicyStatus::TooLong;
        return result;
    }
    memcpy(workspace->current_url, url, strlen(url) + 1U);

    for (unsigned redirect = 0U; redirect <= kMaximumHttpsRedirects;
         ++redirect) {
        const UpdateUrlPurpose purpose =
            redirect == 0U ? initial_purpose : UpdateUrlPurpose::Redirect;
        result.url_status =
            ParseAndAuthorizeUpdateUrl(workspace->current_url, purpose,
                                       &workspace->parsed_url);
        if (result.url_status != UrlPolicyStatus::Ok) {
            result.status = redirect == 0U ? HttpsGetStatus::UrlRejected
                                           : HttpsGetStatus::RedirectRejected;
            return result;
        }
        ParsedUpdateUrl &parsed = workspace->parsed_url;
        SetString(result.final_host, sizeof(result.final_host), parsed.host);
        workspace->connect_error[0] = '\0';
        if (!NetworkCheckpoint(foreground_progress, request_kind, 0U, 0U)) {
            result.status = HttpsGetStatus::Canceled;
            return result;
        }
        SecureByteStream *stream = factory->ConnectVerified(
            parsed.host, parsed.port, workspace->connect_error,
            sizeof(workspace->connect_error));
        if (stream == 0) {
            result.status = foreground_progress != 0 &&
                                    foreground_progress->cancel_requested()
                                ? HttpsGetStatus::Canceled
                                : HttpsGetStatus::ConnectFailed;
            return result;
        }
        if (!NetworkCheckpoint(foreground_progress, request_kind, 0U, 0U)) {
            stream->Close();
            factory->Destroy(stream);
            result.status = HttpsGetStatus::Canceled;
            return result;
        }

        char authorization[560U];
        authorization[0] = '\0';
        if (redirect == 0U && options.bearer_token != 0 &&
            strcmp(parsed.host, "api.github.com") == 0) {
            const int authorization_size = snprintf(
                authorization, sizeof(authorization),
                "Authorization: Bearer %s\r\n", options.bearer_token);
            if (authorization_size < 0 ||
                static_cast<size_t>(authorization_size) >=
                    sizeof(authorization)) {
                stream->Close();
                factory->Destroy(stream);
                memset(authorization, 0, sizeof(authorization));
                result.status = HttpsGetStatus::RequestTooLarge;
                return result;
            }
        }
        char content_headers[256U];
        content_headers[0] = '\0';
        if (strcmp(options.method, "POST") == 0) {
            const int content_size = snprintf(
                content_headers, sizeof(content_headers),
                "Content-Type: %s\r\nContent-Length: %llu\r\n",
                options.content_type,
                static_cast<unsigned long long>(options.body.size));
            if (content_size < 0 ||
                static_cast<size_t>(content_size) >= sizeof(content_headers)) {
                stream->Close();
                factory->Destroy(stream);
                memset(authorization, 0, sizeof(authorization));
                result.status = HttpsGetStatus::RequestTooLarge;
                return result;
            }
        }
        const char *accept = options.accept != 0
                                 ? options.accept : AcceptHeader(request_kind);
        const int request_header_size = snprintf(
            workspace->request, sizeof(workspace->request),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: BMX-Updater/1\r\n"
            "Accept: %s\r\n"
            "Accept-Encoding: identity\r\n"
            "%s"
            "%s"
            "%s"
            "Connection: close\r\n\r\n",
            options.method, parsed.path_and_query, parsed.host, accept,
            strcmp(parsed.host, "api.github.com") == 0
                ? "X-GitHub-Api-Version: 2022-11-28\r\n"
                : "",
            authorization, content_headers);
        memset(authorization, 0, sizeof(authorization));
        if (request_header_size < 0 ||
            static_cast<size_t>(request_header_size) >=
                                   sizeof(workspace->request) ||
            options.body.size >
                sizeof(workspace->request) -
                    static_cast<size_t>(request_header_size)) {
            stream->Close();
            factory->Destroy(stream);
            memset(workspace->request, 0, sizeof(workspace->request));
            result.status = HttpsGetStatus::RequestTooLarge;
            return result;
        }
        const size_t request_size =
                                    static_cast<size_t>(request_header_size) +
                                    options.body.size;
        if (options.body.size != 0U) {
            memcpy(workspace->request + request_header_size,
                   options.body.data,
                   options.body.size);
        }
        bool send_canceled = false;
        if (!SendAll(stream,
                     reinterpret_cast<const uint8_t *>(workspace->request),
                     request_size, request_kind,
                     foreground_progress, &send_canceled)) {
            memset(workspace->request, 0, request_size);
            stream->Close();
            factory->Destroy(stream);
            result.status = send_canceled ? HttpsGetStatus::Canceled
                                          : HttpsGetStatus::SendFailed;
            return result;
        }
        memset(workspace->request, 0, request_size);

        size_t header_size = 0U;
        size_t header_bytes = 0U;
        HttpResponseHead &head = workspace->response_head;
        bool parsed_head = false;
        while (!parsed_head) {
            if (!NetworkCheckpoint(foreground_progress, request_kind, 0U,
                                   0U)) {
                result.status = HttpsGetStatus::Canceled;
                break;
            }
            if (header_size == sizeof(workspace->headers)) {
                result.header_status = HttpParseStatus::HeaderTooLarge;
                break;
            }
            const int received = stream->Receive(
                workspace->headers + header_size,
                sizeof(workspace->headers) - header_size);
            if (received <= 0 ||
                static_cast<size_t>(received) >
                    sizeof(workspace->headers) - header_size) {
                result.status = HttpsGetStatus::ReceiveFailed;
                break;
            }
            header_size += static_cast<size_t>(received);
            if (!NetworkCheckpoint(foreground_progress, request_kind, 0U,
                                   0U)) {
                result.status = HttpsGetStatus::Canceled;
                break;
            }
            result.header_status = ParseHttpResponseHead(
                workspace->headers, header_size, &head, &header_bytes);
            if (result.header_status == HttpParseStatus::Ok) {
                parsed_head = true;
            } else if (result.header_status != HttpParseStatus::NeedMoreData) {
                break;
            }
        }
        if (!parsed_head) {
            stream->Close();
            factory->Destroy(stream);
            if (result.status != HttpsGetStatus::ReceiveFailed &&
                result.status != HttpsGetStatus::Canceled) {
                result.status = HttpsGetStatus::HeaderInvalid;
            }
            return result;
        }
        result.http_status = head.status_code;

        if (IsRedirectStatus(head.status_code)) {
            stream->Close();
            factory->Destroy(stream);
            if (strcmp(options.method, "GET") != 0) {
                result.status = HttpsGetStatus::RedirectRejected;
                return result;
            }
            if (redirect == kMaximumHttpsRedirects) {
                result.status = HttpsGetStatus::TooManyRedirects;
                return result;
            }
            result.url_status = ParseAndAuthorizeUpdateUrl(
                head.location, UpdateUrlPurpose::Redirect,
                &workspace->redirect_url);
            if (result.url_status != UrlPolicyStatus::Ok ||
                strlen(head.location) >= sizeof(workspace->current_url)) {
                result.status = HttpsGetStatus::RedirectRejected;
                return result;
            }
            memcpy(workspace->current_url, head.location,
                   strlen(head.location) + 1U);
            result.redirects = redirect + 1U;
            continue;
        }
        if (head.status_code != 200U) {
            stream->Close();
            factory->Destroy(stream);
            result.status = HttpsGetStatus::UnexpectedStatus;
            return result;
        }

        HttpBodyDecoder decoder(head, maximum_body_bytes, sink);
        size_t consumed = 0U;
        result.body_status = decoder.Feed(workspace->headers + header_bytes,
                                          header_size - header_bytes,
                                          &consumed);
        const uint64_t progress_total = head.has_content_length
            ? head.content_length : 0U;
        if (!NetworkCheckpoint(foreground_progress, request_kind,
                               decoder.decoded_bytes(), progress_total)) {
            stream->Close();
            factory->Destroy(stream);
            result.status = HttpsGetStatus::Canceled;
            return result;
        }
        if (result.body_status == HttpBodyStatus::UnexpectedData ||
            result.body_status == HttpBodyStatus::InvalidArgument ||
            result.body_status == HttpBodyStatus::TooLarge ||
            result.body_status == HttpBodyStatus::SinkRejected ||
            result.body_status == HttpBodyStatus::InvalidChunk) {
            stream->Close();
            factory->Destroy(stream);
            result.status = HttpsGetStatus::BodyInvalid;
            return result;
        }
        while (!decoder.complete()) {
            if (!NetworkCheckpoint(foreground_progress, request_kind,
                                   decoder.decoded_bytes(), progress_total)) {
                result.status = HttpsGetStatus::Canceled;
                break;
            }
            const int received = stream->Receive(
                workspace->receive, sizeof(workspace->receive));
            if (received <= 0 || static_cast<size_t>(received) >
                                     sizeof(workspace->receive)) {
                result.body_status = decoder.Finish();
                break;
            }
            consumed = 0U;
            result.body_status = decoder.Feed(
                workspace->receive, static_cast<size_t>(received), &consumed);
            if (!NetworkCheckpoint(foreground_progress, request_kind,
                                   decoder.decoded_bytes(), progress_total)) {
                result.status = HttpsGetStatus::Canceled;
                break;
            }
            if (result.body_status != HttpBodyStatus::Ok &&
                result.body_status != HttpBodyStatus::Complete) {
                break;
            }
        }
        stream->Close();
        factory->Destroy(stream);
        result.body_bytes = decoder.decoded_bytes();
        if (result.status == HttpsGetStatus::Canceled) return result;
        if (!decoder.complete() ||
            (result.body_status != HttpBodyStatus::Ok &&
             result.body_status != HttpBodyStatus::Complete)) {
            result.status = HttpsGetStatus::BodyInvalid;
            return result;
        }
        result.body_status = HttpBodyStatus::Complete;
        result.status = HttpsGetStatus::Ok;
        return result;
    }
    result.status = HttpsGetStatus::TooManyRedirects;
    return result;
}

HttpsGetResult HttpsGet(const char *url, UpdateUrlPurpose initial_purpose,
                        HttpsRequestKind request_kind,
                        uint64_t maximum_body_bytes, HttpBodySink *sink,
                        SecureStreamFactory *factory,
                        HttpsGetWorkspace *workspace,
                        UpdateForegroundProgress *foreground_progress) {
    const HttpsRequestOptions options;
    return HttpsRequest(url, initial_purpose, request_kind, options,
                        maximum_body_bytes, sink, factory, workspace,
                        foreground_progress);
}

const char *HttpsGetStatusString(HttpsGetStatus status) {
    switch (status) {
        case HttpsGetStatus::Ok: return "ok";
        case HttpsGetStatus::InvalidArgument: return "invalid argument";
        case HttpsGetStatus::UrlRejected: return "update URL rejected";
        case HttpsGetStatus::ConnectFailed: return "verified TLS connection failed";
        case HttpsGetStatus::RequestTooLarge: return "HTTP request too large";
        case HttpsGetStatus::SendFailed: return "HTTPS send failed";
        case HttpsGetStatus::ReceiveFailed: return "HTTPS receive failed";
        case HttpsGetStatus::HeaderInvalid: return "HTTP response header invalid";
        case HttpsGetStatus::RedirectRejected: return "HTTPS redirect rejected";
        case HttpsGetStatus::TooManyRedirects: return "too many HTTPS redirects";
        case HttpsGetStatus::UnexpectedStatus: return "unexpected HTTP status";
        case HttpsGetStatus::BodyInvalid: return "HTTP response body invalid";
        case HttpsGetStatus::Canceled: return "HTTPS request canceled";
    }
    return "unknown HTTPS error";
}

}  // namespace update
}  // namespace bmx
