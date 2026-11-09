#pragma once
// The picture of a carried thing: drawn on the hand, while its body stays in PhysX.
#include <cstdint>

namespace cvr::heldpicture {
// A carried body, as the script knows it: its proxy and body index, the bone of the hand, and where the thing sits in that hand's frame.
// False if this kind of thing cannot be drawn this way (or the setting is off): then it stays as it was, its picture on its body.
// Blocked: the body has run into something, and the picture goes back to it. Reach: how far from its body the picture may be, m, for a wall ahead of it.
bool Hold(uint32_t proxy,uint32_t body,int bone,const float offset[3],const float turn[4],bool blocked,float reach);
// What the solver held back this step, world: the mesh is drawn at the hand plus this, so it agrees with walls and with weight without any blending. Zero when free.
void Correct(uint32_t proxy,uint32_t body,const float dp[3],const float dq[4],int side,const float handFix[4]);
// The grip has opened: from now on the picture goes with the body, closing what is between them.
void LetGo(uint32_t proxy,uint32_t body);
// The instance behind a proxy, or null if it is no prop of this kind.
uint8_t* Instance(uint32_t proxy);
// The real mesh out of sight while a copy rides the hand (the carriers on the player).
void Hide(uint32_t proxy,uint32_t body,bool hide);
// THE HAND SLOT HELD STILL UNDER A COPY. The script asks every tick (hand 0 left, 1 right); unasked for 250 ms it ends by itself.
void SlotAsk(int side);
// In the pose pass, parent-local bones: the first pass after the ask remembers the slot, the others write it back and count what the animation tried.
void SlotPass(uint8_t* bones,int rightSlot,int leftSlot);
// On the game thread at the frame submit, the last moment before this frame's proxies are flushed.
void AtFrameSubmit();
}
