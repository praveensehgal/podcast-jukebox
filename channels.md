# Channels

The 20 channels the jukebox shows, in screen order. Edit this file, commit to `main`,
and every device picks it up on its next boot (and every 6 hours while running).

Rules the device parser relies on:

- One table only. Rows are `| position | Channel name | RSS feed URL |`.
- Names are what he sees on screen; keep them under 24 characters or they get clipped.
- Feed must be a public RSS/Atom URL starting with `https://`. No Spotify-only shows.
- Up to 20 rows. Blank the row to leave a slot empty; do not renumber the others.

The `channels` workflow checks every feed answers with XML before publishing, so a typo
fails in GitHub, not on his coffee table.

| # | Channel | Feed |
|---|---------|------|
| 1 | NPR News Now | https://feeds.npr.org/500005/podcast.xml |
| 2 | Up First | https://feeds.npr.org/510318/podcast.xml |
| 3 | The Daily | https://feeds.simplecast.com/54nAGcIl |
| 4 | BBC Global News | https://podcasts.files.bbci.co.uk/p02nq0gn.rss |
| 5 | Marketplace | https://feeds.publicradio.org/public_feeds/marketplace/rss/rss |
| 6 | Fresh Air | https://feeds.npr.org/381444908/podcast.xml |
| 7 | This American Life | https://www.thisamericanlife.org/podcast/rss.xml |
| 8 | Radiolab | https://feeds.simplecast.com/EmVW7VGp |
| 9 | Planet Money | https://feeds.npr.org/510289/podcast.xml |
| 10 | Hidden Brain | https://feeds.npr.org/510308/podcast.xml |
| 11 | Freakonomics Radio | https://feeds.simplecast.com/Y8lFbOT4 |
| 12 | Stuff You Should Know | https://omnycontent.com/d/playlist/e73c998e-6e60-432f-8610-ae210140c5b1/A91018A4-EA4F-4130-BF55-AE270180C327/44710ECC-10BB-48D1-93C7-AE270180C33E/podcast.rss |
| 13 | 99% Invisible | https://feeds.simplecast.com/BqbsxVfO |
| 14 | Conan O'Brien | https://feeds.simplecast.com/dHoohVNH |
| 15 | SmartLess | https://rss.art19.com/smartless |
| 16 | Pod Save America | https://feeds.simplecast.com/dxZsm5kX |
| 17 | The Rewatchables | https://feeds.megaphone.fm/the-rewatchables |
| 18 | Bill Simmons | https://feeds.megaphone.fm/the-bill-simmons-podcast |
| 19 | WTF with Marc Maron | https://feeds.acast.com/public/shows/wtf-with-marc-maron-podcast |
| 20 | Pardon My Take | https://feeds.megaphone.fm/pardon-my-take |
