# Driverse — User Guide

This guide walks through using Driverse safely. The application is designed so
that you can never lose data by accident: everything destructive is previewed,
confirmed, audited, and (by default) recoverable from the trash.

## 1. The dashboard

When Driverse starts it lists your local filesystems (mount point, device type,
total/used/free, usage %). Remote filesystems are marked *(remote)* and
pseudo-filesystems like `proc`/`sys` are marked *(virtual)* and greyed out.

- Select a filesystem to see its capacity on the cards and the usage donut.
- `Refresh` re-reads the mount table (useful after plugging in a device).
- Hovering a card explains what it shows. **EST. RECOVERABLE** is a
  *conservative* estimate: bytes moved to the trash only return to free space
  once the trash is emptied, and hard-linked or shared data may free less.

## 2. Scanning

1. Pick a target: select a filesystem in the list, or press **Select Folder...**
   (Ctrl+O) for a specific directory.
2. Press **Scan** (F5). The scan runs in the background; you can keep using the
   window and **Cancel** (Esc) at any time.
3. The status bar shows progress; the **EST. RECOVERABLE** card fills in when
   the scan completes.

Notes:

- Scans are **read-only**. Nothing is ever modified by scanning.
- Symbolic links are never followed. The scan stays on one filesystem.
- Protected system directories are skipped intentionally. Permission errors
  and filesystem cycles are skipped too — see **Scan ▸ Skipped paths…** for
  the full list and the reason for each entry.
- Scanning a **network filesystem** asks for confirmation first, because
  results can be slow and may change underneath you.

## 3. Browsing results

The lower area has three tabs (Ctrl+1 / Ctrl+2 / Ctrl+3):

| Tab | Shows |
|---|---|
| **Largest Files** | The biggest files found, logical vs allocated size |
| **Largest Directories** | Per-directory totals, hard-link deduplicated |
| **Recommendations** | Cleanup candidates with risk, category, justification |

Use the filter bar to narrow things down:

- **Path** contains text (Ctrl+F focuses it)
- **Min size** hides small rows
- **Risk checkboxes** (Safe / Low / Medium / High / Protected)
- **Recommended only** hides rows that have no cleanup suggestion

Right-click a row for *Open containing folder* / *Copy path* / *Copy details*.
Double-click opens the containing folder. The details panel on the right shows
the full metadata (sizes, inode, owner, risk, justification, confidence).

## 4. Understanding recommendations

**Risk levels**

| Level | Meaning |
|---|---|
| Safe | Regenerable cache with strong signals (e.g. thumbnails, old browser cache) |
| Low | Likely disposable; still review |
| Medium | Ambiguous (old downloads, build directories) — review carefully |
| High | May contain user data; strong confirmation required |
| Protected | System-critical — never offered for cleanup |

**Conservative by design.** A directory simply being called `cache` or `tmp` is
never enough — Driverse needs a known location plus ownership/age signals. When
in doubt it errs toward Medium/High rather than Safe. The **Recoverable** column
is the conservative estimate for that item (hard-linked files count as 0, since
removing one name may free nothing).

## 5. Cleaning up

1. On the **Recommendations** tab, check the items you want to remove (start
   with *Safe*, then *Low*; use **Select group** and review).
2. Press **Clean up selected…**.
3. The cleanup dialog shows the **plan**: how many items, the risk breakdown,
   how much is estimated recoverable, and the exact action per item
   (move to trash / permanently delete / skip + reason).
4. Press **Dry run** first if you want a report without touching anything.
5. To apply, work through the **confirmation gates**:
   - tick the acknowledgement (always required),
   - review and tick off **High** risk items (only if present),
   - optionally enable **permanent delete** for eligible Safe caches
     (anything deleted this way is **not** recoverable),
   - type `DELETE` if high-risk items or permanent deletion are involved.
6. **Apply** shows one final warning; after confirming, items are processed:

- everything goes to the **XDG Trash** by default (`~/.local/share/Trash`) and
  can be restored from your file manager;
- if a file changed between preview and apply (same path, different file), the
  batch **aborts** rather than touching the wrong thing;
- after a successful apply, Driverse rescans so the results reflect reality.

Every preview and every apply is recorded in the audit log (see below).

## 6. Audit log & logs

- Audit log: `~/.local/share/driverse/audit.jsonl` — one JSON object per line,
  covering dry runs, apply "begin" records, and results (per-item status,
  bytes, errors).
- Application log: `~/.local/share/driverse/driverse.log`.

The audit log is append-only; Driverse refuses to apply a cleanup if the
"before" record cannot be written. Keep it if you need a history of actions.

## 7. Configuration

Edit `~/.config/driverse/driverse.conf` (created on first save; see the README
for an example). Useful keys:

- `general.min_age_safe_days` — how old a cache must be before it can be Safe.
- `general.allow_permanent_delete` — master opt-in for permanent-delete offers.
- `protected.paths` — additional protected paths (comma-separated), enforced
  in addition to the built-in denylist.

## 8. Troubleshooting

| Symptom | Explanation / action |
|---|---|
| "Target unavailable" | The path was removed/unmounted after you selected it. Reconnect the device, press Refresh, and reselect. |
| "Permission denied" when scanning | You can't read part of that tree. Scan a folder you own, or read **Skipped paths** to see what was excluded. |
| Huge number of skipped paths | Normal for system paths (protected/pseudo-filesystems). Only unexpected entries (your own files) need attention. |
| Cleanup reports "disk full" / "read-only" / "busy" | The failure detail explains what happened: free space, remount read-write, or close the program using the file. |
| "Batch aborted" during apply | A file changed since the preview. Nothing after the changed item was touched; rescan and review again. |
| Trash doesn't free space | Trashed files are recoverable, not gone — empty the trash in your file manager to reclaim the space. |

## 9. Safety guarantees (summary)

- Scanning never modifies anything.
- Protected/system paths can never be selected for deletion.
- Nothing is deleted without a previewed plan, explicit acknowledgements, and
  a final confirmation.
- User files go to the trash by default; permanent deletion is opt-in and only
  possible for regenerable Safe caches.
- Every action is preceded and followed by an audit record.
