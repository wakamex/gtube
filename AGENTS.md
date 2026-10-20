# gtube

Repository-specific decisions; packaging and release rules follow `/code/pacman/RELEASE_POLICY.md`, with build details in `RELEASES.md`.

## Windows builds are unsigned

The Windows executable ships without a code signature, so Microsoft Defender SmartScreen warns on first run and users click "More info", then "Run anyway"; the README does not mention it. This matches Streamlink, which also ships unsigned Windows builds. Signing was declined for now: SignPath's free certificate needs a manual approval for every release, which breaks release automation, and Azure Artifact Signing costs about $10 a month. If Windows users become a real audience, Azure Artifact Signing is the option that keeps releases automatic: it signs inside `publish.yml`, before checksums and attestation.

## Test builds update themselves; releases do not

gtube has two ways out. GitHub Releases are for anyone and carry no updater: a release build has no build name, so it never looks for updates. Test builds are for friends testing with the author: `tools/publish-update` builds the current commit for Windows, names the build after it, and publishes it to the update server at `http://updates.waka.mx/gtube/` (on the Hetzner machine), where every installed test copy fetches it within a minute and starts it at once, going on with the song that was playing from where it was. A first install is `http://updates.waka.mx/gtube/gtube-windows.zip`. Agents run `tools/publish-update` themselves after each commit worth testing, without asking, as on the other apps built on gesso; it refuses a tree with uncommitted changes. The signing key is `~/.config/gtube/update-signing.key` on the author's machine and never goes into the repository or CI.
