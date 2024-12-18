# Troubadour

A Discord Bot for making your DnD sessions more fully audibly immersive! Of no relation to [Bard Audio](https://github.com/gl326/bard-audio).

Built with VS2022 for x64 Windows, using FMOD Studio 2.02 and D++ v10.0.33.

When building locally, be sure to do the following:
- Ensure you've [created a Discord Bot Token](https://dpp.dev/creating-a-bot-application.html) and set everything up on Discord's end.
- Ensure you've renamed MyBot\token.config.example to MyBot\token.config and replaced _all_ the text in it with your Bot Token! The program will read that in at startup and use it when initializing your Bot.
- Make sure the paths in the Post-Build step are correct for where you installed the FMOD API. Adjust them as necessary.

Thanks for checking it out! Please contact me for any questions or feedback via details found on [my Website.](https://loganhardin.xyz/)
