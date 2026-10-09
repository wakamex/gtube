# gtube

Repository-specific decisions; packaging and release rules follow `/code/pacman/RELEASE_POLICY.md`, with build details in `RELEASES.md`.

## Windows builds are unsigned

The Windows executable ships without a code signature, so Microsoft Defender SmartScreen warns on first run and users click "More info", then "Run anyway"; the README does not mention it. This matches Streamlink, which also ships unsigned Windows builds. Signing was declined for now: SignPath's free certificate needs a manual approval for every release, which breaks release automation, and Azure Artifact Signing costs about $10 a month. If Windows users become a real audience, Azure Artifact Signing is the option that keeps releases automatic: it signs inside `publish.yml`, before checksums and attestation.
