# Universe RP website

This branch (`gh-pages`) holds the Universe RP website only, published by GitHub Pages at
https://praviilon.github.io/Universe_RP/. The mod's code lives on `develop` and `main`.

Plain static files, no build step (`.nojekyll` stops Pages from running Jekyll):

- `index.html`, `404.html` and the other pages: each carries its own header and footer.
- `assets/css/site.css`: the one stylesheet.
- `assets/js/starfield.js`: the star field behind every page. `assets/js/site.js`: header, latest-release badge.
- `assets/fonts/`: Orbitron and Exo 2 (SIL Open Font License, licences next to them).
- `assets/img/`: the galaxy (NASA/JPL-Caltech), the link-preview cover and the icons.

To preview locally, open `index.html` in a browser, or run `python -m http.server` here and open
http://localhost:8000/.
