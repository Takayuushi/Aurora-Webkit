#pragma once

#include <WebKit/WKBase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WKPageResumeDownloadCallback)(WKDownloadRef download, const void* functionContext);

// Resume a canceled download using the opaque data returned by WKDownloadCancel.
// path must be the same destination used for the original download.
WK_EXPORT void WKPageResumeDownloadFromResumeData(
    WKPageRef page,
    WKDataRef resumeData,
    WKStringRef path,
    const void* functionContext,
    WKPageResumeDownloadCallback callback);

#ifdef __cplusplus
}
#endif
