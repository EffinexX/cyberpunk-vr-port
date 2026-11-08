#pragma once

// THE GAME'S OWN AUDIO ENGINE, ON A PLACE AND AT A VOLUME OF OUR CHOOSING. Cyberpunk2077.exe exports AK::SoundEngine by name, so a few emitters
// of our own are registered with Wwise, put where a touch happened and given a gain of their own: the game's emitters, and so its gunshots, are not touched.
namespace cvr::wwise {

// One event at a world point (the game's own coordinates), at `gain` (linear; 1 = as authored). `group` / `state`, when not empty, is the Wwise switch set first.
// False when Wwise could not be reached or did not start it: the caller has its own way to play it.
bool Play(const char* event, float x, float y, float z, float gain, const char* group, const char* state);

// A scrape, called each frame it goes on: body 0..2, the event, where, how loud. The game's slide events last 1.5 s or so, so it is started again, overlapping, before it ends;
// a new event, or "" to end it, stops what plays with a short fade. If the calls cease, what plays runs out by itself.
bool Scrape(int body, const char* event, float x, float y, float z, float gain);

}  // namespace cvr::wwise
