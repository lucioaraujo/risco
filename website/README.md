# Risco website

Static release website in Portuguese, English, French and Spanish. It has an
instrument introduction, a first-composition tutorial, stable release
downloads, step-by-step installation pages, authorship and license links.
Each language page links to the Linux `.deb`, AppImage and user-local
`.tar.gz`, plus the Windows and macOS ZIP archives of `v0.1.0`.

The stable release and checksums are attached to the GitHub release at
`https://github.com/lucioaraujo/risco/releases/tag/v0.1.0`.
`robots.txt` allows indexing. Automated build, test, package and Linux startup
checks passed in GitHub Actions run
`https://github.com/lucioaraujo/risco/actions/runs/45873627861`. Windows and
macOS were built and tested in CI but not manually opened on personal devices;
personal audio, devices and interface behavior remain for author evaluation.

The four homepages share `style.css` and show the same 1920×1006 capture
from the 0.1.0 interface in English. The English tutorial explains how to
switch the main controls and help to English.

The RASGO family strip links back to the portal in the page's language.
The site uses the original pixel wordmark from `resources/RiscoPixelLogo.svg`
and the approved asymmetric-wave icons from `resources/icons/`. The web
copies are in `assets/identity/`. Every page declares the SVG, PNG and ICO
favicons and the Apple touch icon. The wordmark appears in the header and
on each home page.
The repository's GitHub Pages workflow publishes the site. The general
RASGO portal uses its own publication process.
