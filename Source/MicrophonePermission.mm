#import <AVFoundation/AVFoundation.h>

#include "MicrophonePermission.h"

void requestMicrophonePermission (std::function<void (bool)> callback)
{
    auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];

    if (status == AVAuthorizationStatusAuthorized)
    {
        callback (true);
        return;
    }

    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted)
    {
        dispatch_async (dispatch_get_main_queue(), ^{
            callback ((bool) granted);
        });
    }];
}
