# Termux video reference check — 2026-10-22

App 0.3.4, source `fb30c80`, Galaxy S24 SM-S921N / Android 17.
GitHub build, lint, unit tests, runtime integrity and browser gesture tests passed:
https://github.com/shin5968/proroot-liunx/actions/runs/39750982771

The supplied 16-second Termux recording shows a single tap opening the keyboard, blue selection handles, a floating system copy/paste toolbar, range dragging, tap dismissal and quick history scrolling. It is a visual interaction reference; its terminal contents were not executed.

Implemented native Android floating selection actions, blue handles with a white selection, toolbar hiding while dragging, edge scrolling for range selection, momentum scrolling with touch-to-stop, a transient scroll indicator, explicit tap-to-open-keyboard and shorter native extra-key rows. Fullscreen terminal behavior is retained.

Physical device checks after updating the APK with app data retained:

| Check | Result |
| --- | --- |
| Tap terminal to open keyboard | PASS; terminal viewport became 401 CSS pixels high |
| Stationary long press | PASS; selected `TERMUX_STYLE_TEST` with blue handles and native toolbar |
| Drag selection handle | PASS; selection extended into the following prompt; keyboard stayed open |
| Tap empty area | PASS; selection cleared |
| Native Copy action and Android clipboard paste | PASS; `TERMUX_STYLE_TEST` appeared in the actual Bash command line |
| Momentum after a physical swipe | PASS; viewport moved from line 153 to 147 during the next 130 ms, with no selection |
| Fatal app log check | No AndroidRuntime errors or fatal libc output observed |

The test command line was cleared without executing pasted text, and the app was returned to a clean prompt with the keyboard hidden.

[Native selection screenshot](device-tests/v0.3.4/native-selection.png)

Browser regression tests also cover both the HTML fallback and native-toolbar bridge, dragging, keyboard requests, scrolling momentum and cancellation, in addition to existing clipboard and touch checks. Android toolbar appearance follows the device's system styling.
