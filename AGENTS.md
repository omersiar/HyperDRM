# Release workflow

For every new app version, publish the latest built ARM IPK as an attachment to
a Gitea release in https://gitea.adnau.de/matze19999/HyperDRM.

- Complete relevant checks and keep app/package versions consistent.
- Build the IPK and regenerate the package manifest.
- Push the version's source and package to `main`.
- Create a release tagged `v<app version>` at the corresponding commit, with
  concise English release notes and the exact versioned IPK attachment.
- Verify the published attachment's size and SHA-256 against the built IPK.
- Reuse an existing release for the same version instead of creating a duplicate.
- Never commit credentials or API tokens. SSH Git access alone does not grant
  access to release creation or attachment upload; use an authorized web session
  or an appropriately scoped existing API credential.

The user has requested release publication as part of each future version.
No additional publication confirmation is needed within that scope.
