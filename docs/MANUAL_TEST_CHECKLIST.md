# Manual Test Checklist

Automated tests cover the logic; this checklist covers what only a real desktop
session can verify. Run it before a release, on a machine whose data you can
afford to lose (or, better, in a VM with a throwaway home directory).

## Setup

- [ ] Build with warnings-as-errors: `cmake -S . -B build -DDRIVERSE_WERROR=ON && cmake --build build -j`
- [ ] `ctest --test-dir build --output-on-failure` — all green
- [ ] `QT_QPA_PLATFORM=offscreen ./build/driverse` runs without immediate exit
- [ ] Launch normally on your desktop session

## Dashboard

- [ ] Device table lists local filesystems with plausible sizes; *remote* /
  *virtual* rows are marked and greyed
- [ ] Selecting a device updates the cards and the usage donut
- [ ] Plug/unplug a USB stick → **Refresh** updates the list
- [ ] Hovering cards/filters/buttons shows the explanatory tooltips

## Scanning

- [ ] Select a folder (Ctrl+O) and scan (F5): progress appears, UI stays
  responsive, results populate
- [ ] Cancel (Esc) mid-scan works and reports "Scan cancelled"
- [ ] Scan `/` (root): completes or is cancelled; no crash, huge numbers of
  protected skips expected
- [ ] **Scan ▸ Skipped paths…** opens; reasons match expectations (protected
  system path, permission denied, ...) and the summary counts add up
- [ ] Select an NFS/SMB mount and scan → network warning appears; declining
  starts nothing
- [ ] (If easy) `umount` a selected target, press Scan → "Target unavailable"
  message, no crash
- [ ] Empty-state message appears before the first scan and when filters match
  nothing; tabs remain switchable (Ctrl+1/2/3)

## Results view

- [ ] Sorting by every column works in all three tabs
- [ ] Filters (path substring Ctrl+F, min size, risk checkboxes, recommended
  only) combine correctly and are responsive
- [ ] Risk column is colour-coded consistently; details panel matches the row
- [ ] Context menu: Open containing folder / Copy path / Copy details work
- [ ] Double-click opens the containing folder

## Cleanup workflow (use a scratch file you don't need!)

- [ ] Check a Safe recommendation → "Clean up selected…" enables
- [ ] Dialog: plan summary, risk breakdown, per-item actions look correct
- [ ] **Dry run** → report says nothing was changed; files still exist
- [ ] Apply without the acknowledgement checkbox → button stays disabled with a
  tooltip explaining why
- [ ] Apply with acknowledgement → final Yes/No warning → item moves to Trash;
  restore it from the file manager to verify recoverability
- [ ] After apply, the window rescans automatically
- [ ] Audit log (`~/.local/share/driverse/audit.jsonl`) gained a `begin` and a
  result record matching what happened
- [ ] Manual *High* risk: check a High candidate → high-risk review checkbox and
  typing `DELETE` are required before Apply enables
- [ ] Permanent delete (only if a Safe+regenerable candidate exists): enabling
  the opt-in forces typing `DELETE`; after apply the file is gone and
  **not** in the Trash
- [ ] (If easy) Make the audit log unwritable (`chmod 000` on the directory) →
  Apply refuses with "Cleanup did not run" and nothing is modified
- [ ] (If easy) Replace one planned file between preview and Apply → batch
  aborts with an identity-change message; later items untouched

## Robustness spot-checks

- [ ] Scanning a folder you cannot read (e.g. `chmod 000`) → clear
  "Permission denied" message before the scan starts
- [ ] Play with size/date display: values use locale-style formatting and are
  readable
- [ ] Resize the window with a large scan loaded; no noticeable lag

## Packaging (when releasing)

- [ ] `cmake --install build --prefix ~/.local` installs binary, `.desktop`
  entry and icon; the app appears in the launcher
- [ ] `cd build && cpack` produces a tarball that contains the same artifacts
