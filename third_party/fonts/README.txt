Roboto-Medium.ttf - Roboto Medium by Christian Robertson (Google), Apache License 2.0
(LICENSE-Apache-2.0.txt). Taken from Dear ImGui's misc/fonts folder
(https://github.com/ocornut/imgui @ e7289367973e97012a247d6b0a6c4c31b2d4216c) and subset with
fontTools' pyftsubset to Latin, Latin-1, Latin Extended-A, Cyrillic and common punctuation:

  pyftsubset Roboto-Medium.ttf --layout-features=kern --output-file=Roboto-Medium.ttf \
    --unicodes="U+0020-007E,U+00A0-00FF,U+0100-017F,U+0400-045F,U+0490-0491,U+2010-2027,U+2030-203A,U+20AC,U+20BD,U+2116,U+2122"

CMake embeds it into BedrockQoL.dll as the menu's built-in font (cmake/embed.cmake).
