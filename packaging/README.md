# packaging

The Vellum packages of this add-on: \`rmpp-hwr\` \`rmpp-hwr-en\` \`rmpp-hwr-ja\` \`rmpp-hwr-ko\` \`rmpp-hwr-latin\` \`rmpp-hwr-zh\` 
[rmpp-packages](https://github.com/lorancecall/rmpp-packages) builds, signs and publishes them from here (its
`scripts/registry.py` finds this repository by the topic `rmpp`). After a release, update `pkgver` and the
sums in the VELBUILD on the default branch; the next build (daily, or
`gh workflow run publish.yml -R lorancecall/rmpp-packages`) publishes it. Format and rules:
[MODS.md](https://github.com/lorancecall/rmpp-packages/blob/main/MODS.md).
