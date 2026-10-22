# Hidden keyboard scroll regression — 2026-10-22

App 0.3.5, source `842c8fa`, Galaxy S24 SM-S921N / Android 17.
Build, lint, unit tests, runtime verification and browser gesture tests passed:
https://github.com/shin5968/proroot-liunx/actions/runs/39752507807

## Reproduction and fix

On 0.3.4, hiding the keyboard and immediately swiping reopened the IME. The touch gesture was correctly classified as moved, and the JavaScript keyboard bridge was never called. The WebView retained its focused textarea and independently reopened the IME.

The host now disables editor/input-connection creation until an explicit keyboard request. An IME visible-to-hidden transition cancels queued keyboard work and blurs the terminal input. Gesture movement also cancels pending delayed keyboard requests. A tap that stops momentum does not open the keyboard. Terminal resizing preserves the current history viewport when it was scrolled away from the bottom.

## Physical device results

Updated the APK without resetting Ubuntu or app data, then tested with 180 lines of real Bash command output.

| Check | Result |
| --- | --- |
| Hide IME and immediately swipe | PASS: viewport height 760 CSS pixels; native mInputShown=false; textarea blurred |
| Repeated swipes with keyboard hidden | PASS: viewport moved through history; keyboard remained hidden |
| Tap to stop momentum | PASS: fling stopped; viewport stayed at line 60; height remained 760 |
| Fresh intentional tap | PASS: keyboard opened; height became 401; viewport stayed at line 60 |
| Hide keyboard while reading history | PASS: height returned to 760; viewport remained at line 60 |
| Type after reopening | PASS: actual Android keyboard entered echo IME_FIX_OK; Bash printed IME_FIX_OK |
| Fatal app log check | No AndroidRuntime errors or fatal libc output observed |

Returned the app to a clean Bash prompt with the keyboard hidden.

Browser tests cover hiding the IME during a gesture, deliberate reopening, momentum-stop taps, cancellation requests and history preservation across opening/closing viewport sizes, in addition to existing touch regressions.
