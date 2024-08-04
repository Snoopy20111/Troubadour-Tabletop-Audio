# Troubadour

A Discord Bot for making your DnD sessions more fully audibly immersive! Of no relation to [Bard Audio](https://github.com/gl326/bard-audio), which is an excellent audio middleware for GameMaker Studio 2.

Built with VS2022 for Windows, x86 and x64, using FMOD Studio 2.02.16 and D++. May work with later versions of FMOD as the API is really stable, but you'll be doing so at your own risk and I intend to update the APIs used every so often. For D++, as the project is forked from a well-maintained Visual Studio template, you may safely assume it's using the latest version of that as well.

When building locally, be sure to do the following:
- Ensure you've [created a Discord Bot Token](https://dpp.dev/creating-a-bot-application.html) and set everything up on Discord's end.
- Ensure you've renamed MyBot\token.config.example to MyBot\token.config and replaced _all_ the text in it with your Bot Token! The program will read that in at startup and use it when initializing your Bot.
- Make sure the paths in the Post-Build step are correct for where you installed the FMOD API. Adjust them as necessary.

Thanks for checking it out! Please contact me for any questions or feedback via details found on [my Website.](https://loganhardin.xyz/)
