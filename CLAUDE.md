# Project notes for Claude

This is a fork of the Metroid: Zero Mission decompilation (`metroidret/mzm`) with
a 3DS port under `platform/3ds/` and tooling under `tools/`.

## Language: English in code

All **comments, function names, and variable names** must be written in English.
This applies to everything we own or add:

- `platform/3ds/` (the port)
- `tools/` (build scripts, the layer workbench, etc.)
- `docs/` and any new files

**Exception:** the upstream decompilation itself — `src/`, `include/`, `asm/`,
`data/`, `sound/`, and the other files that come from `metroidret/mzm`. Leave
those as they are; do not retranslate or churn them.

No project-wide sweep is wanted. Parts of `tools/layer-workbench/` (`build_maps.py`,
`mzmdata.py`, `serve.py`, `README.md`, `index.html`) are still in Spanish from an
earlier convention. Convert a file's comments/identifiers to English **as you
touch it** for other reasons, not as a standalone task. User-facing UI strings and
prose docs can stay as they are until their file is otherwise being reworked.

## Save states

Before adding, renaming or changing a global in `src/` (or anything the save
thread or audio touches), read `docs/3ds-save-states.md`: states are saved by
variable name and their host pointers are rebuilt on load, so a global that
holds a pointer or a runtime handle may need a rule in
`platform/3ds/source/port_state_ptrs.c`.

## Git remotes: `origin` is the one that matters

`origin` is `tinaut1986/mzm` (this project). `upstream` is `metroidret/mzm`, the
decompilation this repo forked from — it is a **different project**, and nothing
we do here is ever aimed at it. Never open a PR, push, or target an issue
against `upstream` unless explicitly told to.

Because GitHub knows this repo is a fork, `gh` resolves a bare command to the
fork *parent* by default. `remote.origin.gh-resolved=base` is set in
`.git/config` to override that, so plain `gh run list` / `gh pr create` /
`gh release view` act on `tinaut1986/mzm`. That setting is local config, not
checked in — if a fresh clone starts resolving to `metroidret/mzm` again, run:

```sh
gh repo set-default tinaut1986/mzm
```

## Release process (3DS port)

Version numbers are meaningful, not sequential: a **minor** bump marks a
milestone (v0.4.0 followed v0.3.4 because the port became playable start to
finish), a **patch** is an ordinary fix round. Ask before choosing a minor bump;
never infer one. The next release branch is `release/vX.Y.(Z+1)`.

`main` means **stable**. A `release/*` branch is where work accumulates, and it
stays off `main` for as long as there are known things left to resolve — being
on `release/*` is itself the statement "not stable yet".

### Publishing

**First, for either path: write `docs/release-notes/<tag>.md`** (see "Release
notes" below) on a topic branch, show it to the user, and merge it into the
release branch *before* the tag is created. The tag must contain the file, and
a notes file is never added after tagging. Do this unprompted whenever a tag is
about to be made; the steps below assume it is already in the branch.

Two paths. Which one applies depends on whether the branch is ready to be called
stable, so **ask** rather than assuming.

**Beta** — tag the `release/*` branch without merging. Publishes a pre-release.
Rename the branch to the next patch right away (see "After tagging"): the tag
fixes what that version contains, and anything done afterwards belongs to the
next one.

```sh
git tag -a v0.4.4 -m "v0.4.4"
git push origin release/v0.4.4
git push origin v0.4.4          # -> GitHub pre-release, "Beta v0.4.4"
```

**Stable** — merge into `main` first, then tag the merge commit.

```sh
git checkout main
git merge --no-ff release/v0.4.4
git tag -a v0.4.4 -m "v0.4.4"
git push origin main            # main FIRST -- see below
git push origin v0.4.4          # -> GitHub release, "Release v0.4.4"
```

A tag that already shipped as a beta is promoted in place: once its commit is
reachable from `main`, re-run **Build 3DS Release** from the Actions tab and
pick **the tag itself** as the ref, not `main`. The workflow derives the release
name from `git describe` on a manual run, so dispatching on `main` resolves to
`vX.Y.Z-1-g<hash>` (the merge commit is one commit past the tag) and publishes a
second, wrongly-named page instead of promoting the existing one. Dispatching on
the tag gives the bare `vX.Y.Z`, and `main` now reaches it, so the same page
flips to stable.

### Release notes (shown in the 3DS updater)

Every tag needs `docs/release-notes/vX.Y.Z.md`, written **before** tagging.
The CI copies it into the release body between `<!-- mzm-notes -->` markers;
the console's updater (UPDATES > WHAT'S NEW) reads that block from the GitHub
releases response and shows the notes of every release newer than the build
the player has installed. A missing file only raises a CI warning, and the
console then shows "(no notes for this version)" for that release.

How to write it:

- **For the player, in English**, 3 to 6 short lines, each starting with `- `.
  What changed in play, not how: no file names, no issue jargon. Draft it from
  `git log <previous tag>..HEAD`, then condense. The user reviews it in the diff.
- Plain text only: the console draws it with a 5x7 bitmap font, so no tables,
  images or links; `**bold**` and backticks are stripped, `#` headings dropped.
- **A stable release lists everything since the previous *stable* tag**,
  including what already shipped in betas, because stable players never see the
  beta pages. A beta lists only what is new since the previous tag.
- Keep each file under ~2.5 KB (the updater reads at most 3 KB per release and
  8 releases per check).

`tools/update-mock-server.py` serves releases with such a block, to try the
viewer on a console without publishing anything.

### How the CI picks the channel

`.github/workflows/build-release.yml` runs on any `v*` tag push and on manual
dispatch. It marks the GitHub Release as a pre-release when **`main` cannot
reach the built commit** — not by whether a tag was pushed:

| Built from | Channel |
|---|---|
| Tag on an unmerged `release/*` branch | `Beta` (pre-release) |
| Tag on `main`, or on a commit merged into `main` | `Release` |
| Manual dispatch on any branch | `Beta` (pre-release) |

**The channel is baked into the binary.** The workflow works the channel out
*before* compiling and passes it as `make CHANNEL=beta|release`; a beta CIA
shows `vX.Y.Z BETA` (Options footer, UPDATES, the update prompt) and a stable
one shows plain `vX.Y.Z`. The updater treats a beta of `X.Y.Z` as older than the
stable `X.Y.Z` (`Updater_IsNewerBuild`), so promoting a beta in place (see
above) makes installed betas offer the stable build, while a beta never offers
to install its own beta page. Builds from before v0.7.2 carry no marker.

**Push `main` before the tag.** The tag build resolves the channel against
`origin/main`, so a tag that arrives first cannot see the merge and publishes as
a beta. Also note Actions uses the workflow file **at the tagged commit**, so a
change to the workflow only takes effect for tags cut after it landed.

### After tagging

**After every tag, beta or stable, do this immediately — before any further
work or build.** Rename the release branch to the next patch version and delete
the old remote branch — the tag identifies that line from then on:

```sh
git branch -m release/v0.4.4 release/v0.4.5
git branch --unset-upstream                  # the rename keeps the old tracking ref
git push -u origin release/v0.4.5
git push origin --delete release/v0.4.4
```

Why it cannot wait: the build version comes from the branch name. Left on
`release/v0.4.4` after tagging `v0.4.4`, every later dev build calls itself
`v0.4.4-dev.N`, so the console keeps offering the already-published `v0.4.4` as
an update. A new tag after the rename is the only way later work ships.

**Promoting a beta in place is only valid if nothing was committed after its
tag.** The promotion rebuilds *the tag's* commit; commits made since would be
merged into `main` yet absent from the "stable" `vX.Y.Z`. If there are any,
tag the next version instead (and write its notes).

The version baked into the build (`make print-version`, and the FTP upload
filename) is derived from git: an exact tag on `HEAD` gives `vX.Y.Z`, anything
else on a release branch gives `vX.Y.Z-dev.<commits>+<hash>`.

## The console in the loop (for Claude)

A debug build (`DEBUG_TOOLS=1`) running on the owner's console can be reached from this machine
(same LAN, the IP the build assistant saved) once **DEBUG -> HERRAMIENTAS -> FTP SERVER** is on;
it comes back by itself after a relaunch. Nothing needs FBI or closing the game:

```sh
python3 tools/build_3ds.py --install <ip> --no-clean       # build, upload, install over the running game, relaunch
curl -u mzm3ds:LdHrf1259oikAkPg "ftp://<ip>:5000/3ds/Metroid%20Zero%20Mission%203DS/debug/"      # list / fetch the debug files
tools/mzm-link/mzm-link.py wait && tools/mzm-link/mzm-link.py run SCRIPT   # drive the game
```

**The FTP login** (debug builds and the install-only server below): user `mzm3ds`, password
`LdHrf1259oikAkPg`, defined in `platform/3ds/source/ftp_credentials_3ds.h` (the tools read it from there; keep
both in step). Plain `curl` needs `-u mzm3ds:LdHrf1259oikAkPg`. The password is in the repository and the binary:
it keeps an accidental client out, it is not a secret.

**Production builds** have no FTP unless `dev-ftp.txt` exists in `/3ds/Metroid Zero Mission 3DS/`
(the owner creates it once, over FTP, while a debug build is installed): then they start an
install-only server (only `/3ds/Metroid Zero Mission 3DS/update/`, no remote control, the address
and a CANCEL button on the bottom screen), enough for `build_3ds.py --install`. See
`docs/3ds-debug-tools.md`. Players never see it; do not put it in the release notes.

`tools/mzm-link/mzm-link.py` presses buttons, taps, saves and loads states, takes screenshots and
reads state and memory (`docs/3ds-debug-tools.md`, "FTP server, install and remote control").
Say before driving the game or installing while the owner is playing: an install loses unsaved
progress. If the connection is refused, ask for the address the FTP SERVER cell shows; do not scan
the network without need. The first build with the server arrives the old way (FBI / ftpd).

