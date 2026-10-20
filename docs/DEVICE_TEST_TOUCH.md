# Terminal touch regression check — 2026-10-20

App 0.3.3, source `fadf9a1`, Samsung Galaxy S24 (SM-S921N), Android 17.
Build, lint, unit tests, runtime integrity verification and the new browser touch job passed:
https://github.com/shin5968/proroot-liunx/actions/runs/39413950164

## Fixed

- Touch scrolling and long-press selection now have one gesture handler. Moving more than 8 CSS pixels cancels the selection timer. Android context-menu events following touch cannot select text.
- A short tap outside selection controls clears the selection and toolbar. Previously all clicks were swallowed while selecting.
- Touch cancellation and multiple fingers cancel pending selection. A pause after starting a swipe cannot trigger selection.
- Selection handles cannot cross and exchange their meanings. Resizing clears obsolete selection coordinates.
- Clipboard paste bypasses CTRL/ALT key transformations and retains xterm bracketed-paste handling.
- Native Compose extra keys replace the second WebView. Modifier highlighting follows the terminal's actual modifier state, including after hiding and restoring the key row.

## Physical device results

Installed with `adb install -r`; existing Ubuntu retained. Commands ran in the real Bash session.

| Check | Result |
| --- | --- |
| Swipe through 150 command-output lines | Viewport moved from line 107 to 88; no selection |
| Stationary long press | Selected `SCROLL_TEST_095`; toolbar visible |
| Tap empty area | Selection empty; toolbar hidden |
| Native CTRL + keyboard C | Interrupted `sleep 30`; prompt returned |
| Keyboard focus while using native CTRL | Keyboard remained visible; terminal viewport height stayed 385 CSS pixels |
| Long-press copy then long-press paste menu | `COPY_PASTE_OK` pasted into Bash command line; selection dismissed |

The pasted test text was cleared without executing it. The final screen was returned to a clean Bash prompt.

## Automated coverage

`scripts/terminal-tests/touch.cjs` drives Chromium touch events and checks swipe scrolling, tap dismissal, stationary selection, canceled holds, pausing during a swipe, selection handles, clipboard actions, bracketed paste and modifier behavior. Clipboard bridge calls are stubbed in browser tests; the physical-device test above checks Android clipboard integration.

Run from the repository root:

```sh
npm ci --prefix scripts/terminal-tests
npx --prefix scripts/terminal-tests playwright install chromium
npm test --prefix scripts/terminal-tests
```

Set `TERMINAL_TEST_BROWSER` to an existing Chromium executable to use that browser instead of Playwright's bundled one.
