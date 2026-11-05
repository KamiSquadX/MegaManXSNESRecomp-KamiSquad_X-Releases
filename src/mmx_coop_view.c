#include "mmx_coop_view.h"
#include "mmx_wide_policy.h"

static bool online;
/* Agreed mod option; actual session gating forces shared views offline. */
static bool independent=false;
void MmxCoopViewsSetIndependent(bool enabled) { independent=enabled; }
bool MmxCoopViewsIndependent(void) { return independent; }
static MmxCoopViewWorldState world;
static MmxCoopViewBoundsState bounds;
static const uint8_t *rom;
static size_t rom_size;
void MmxCoopViewsSetRom(const uint8_t *bytes,size_t size) {rom=bytes;rom_size=size;}
MmxCoopViewBoundsState MmxCoopViewsGetBoundsState(void) {return bounds;}
bool MmxCoopViewsValidBoundsState(const MmxCoopViewBoundsState *b) {
  if(!b || b->valid>3 || b->stage>12) return false;
  for(unsigned seat=0;seat<2;++seat) for(unsigned i=0;i<4;++i)
    if((b->valid&(1u<<seat)) && b->limits[seat][i]>8192) return false;
  return true;
}
void MmxCoopViewsSetBoundsState(const MmxCoopViewBoundsState *b) {bounds=*b;}
MmxCoopViewWorldState MmxCoopViewsGetWorldState(void) { return world; }
void MmxCoopViewsSetWorldState(const MmxCoopViewWorldState *s) { world=*s; }
void MmxCoopViewsResetWorld(void) { memset(&world,0,sizeof(world));memset(&bounds,0,sizeof(bounds)); }
void MmxCoopViewsActorReturn(unsigned player) { world.actor_return=(uint8_t)player; }
void MmxCoopViewsContactPlayer(unsigned player) { world.contact_player=(uint8_t)player; }
void MmxCoopViewsBeginStage(unsigned stage) {
  if(!world.initialized || world.stage!=stage) {
    MmxCoopViewsResetWorld();world.stage=(uint8_t)stage;world.initialized=1;
  }
}
bool MmxCoopViewsSeen(unsigned flag) {
  return flag>=0xf800 && flag<=0xffff && (world.seen[(flag-0xf800)>>3]&(1u<<(flag&7)));
}
void MmxCoopViewsMark(unsigned flag,bool seen) {
  if(flag<0xf800 || flag>0xffff) return;
  unsigned n=(flag-0xf800)>>3,bit=1u<<(flag&7);
  if(seen) world.seen[n]|=bit;else world.seen[n]&=~bit;
}
void MmxCoopViewsSetOnline(bool active) { online=active; }
bool MmxCoopViewsOnline(void) { return online; }
static int word(const uint8_t *r) { return r[0]|r[1]<<8; }
static void putword(uint8_t *r,unsigned value) {r[0]=(uint8_t)value;r[1]=(uint8_t)(value>>8);}
static int clamp(int v,int lo,int hi) { if(hi<lo) hi=lo;return v<lo?lo:v>hi?hi:v; }
static bool living(const MmxCoopPlayer *p) {
  return p->status==MMX_COOP_ALIVE && (p->body[0x27]&127) && p->body[2]!=12;
}
static unsigned rom_word(unsigned address) {
  size_t offset=0x30000+(address&0x7fff);
  return rom && offset+2<=rom_size ? (unsigned)word(rom+offset) : 0;
}
static unsigned rom_byte(unsigned address) {
  size_t offset=0x30000+(address&0x7fff);
  return rom && offset<rom_size ? rom[offset] : 0;
}
static void player_limits(const uint8_t *r,const MmxCoopState *s,unsigned seat,
                          const MmxCoopViewBoundsState *b,int out[4]) {
  for(unsigned i=0;i<4;++i) out[i]=word(r+0x1e56+i*2);
  /* The world actor's room is the native one: stage scripts widen it (the
   * Highway airship opens $1E60 to $1D00), which region history never sees.
   * His own tracked copy clamped him mid-screen at 6912+248 = 7160. */
  if(online && s->initialized && (!s->scene_owner || (s->scene_stay && seat!=s->anchor)) &&
      !s->stage_pending && seat!=s->anchor &&
      seat<2 && b->stage==r[0x1f7a] && (b->valid&(1u<<seat)))
    for(unsigned i=0;i<4;++i) out[i]=b->limits[seat][i];
}
void MmxCoopViewsPlayerLimits(const uint8_t *r,const MmxCoopState *s,unsigned seat,int out[4]) {
  player_limits(r,s,seat,&bounds,out);
}
void MmxCoopViewsUpdateBounds(const uint8_t *r,const MmxCoopState *s) {
  if(!online || !s->initialized || r[0x1f7a]>12) return;
  unsigned stage=r[0x1f7a];
  if(bounds.stage!=stage) {memset(&bounds,0,sizeof(bounds));bounds.stage=(uint8_t)stage;}
  /* Native scripted rooms and transport still own both cameras. Re-seed
   * after they release their locks; menus preserve the travel history.
   * Vile's arena lock ($83:E842) is script-set, absent from region history. */
  /* A partner sitting the scene out keeps his own region until it ends. */
  if(s->scene_owner && s->scene_stay && !s->stage_pending && r[0xd3]==4) {
    bounds.valid&=(uint8_t)(1u<<(s->anchor^1));return;
  }
  if(s->scene_owner || s->stage_pending || r[0xd3]!=4) {bounds.valid=0;return;}
  if(s->menu_owner || r[0x1f10]>=6) return;
  /* A boss/miniboss encounter seizes the NATIVE camera with script-set
   * limits that region history cannot reproduce, so a seat inside that
   * arena must use the native limits. That is the world actor, plus a
   * partner standing in the same arena. A partner elsewhere in the stage
   * keeps its own region: falling back to the native limits clamped him
   * into the world actor's screen -- a far-ahead X was thrown back to the
   * start when a miniboss woke for him (or the other way round). */
  uint8_t arena=0;
  for(unsigned d=0xe68;d<0x1228;d+=64) if(r[d] &&
      (MmxWidePolicy_IsBossEncounter(r[d+10]) || (stage==9 && (r[d+10]==0x67 || r[d+10]==0x69)) ||
       (stage==0 && r[d+10]==0x32) || /* Highway's Vile in his ride armor */
       (stage==1 && r[d+10]==0x21) || (stage==2 && r[d+10]==9))) {
    arena=(uint8_t)(1u<<s->anchor);
    /* The arena's invisible walls hold only a player who is inside it: the
     * world actor, plus a partner standing on the world actor's screen
     * between those walls. A partner outside (even just outside) keeps his
     * own region, so the walls never block him from walking in; one step
     * past the wall line and the arena's limits hold him too. The limits
     * themselves cannot tell: while an intro is still setting its lock they
     * span the whole stage. */
    int cam_x=word(r+0x1e4d),cam_y=word(r+0x1e50);
    unsigned other=s->anchor^1u;
    const uint8_t *ob=other==s->current?r+0xba8:s->players[other].body;
    int dx=(int16_t)(word(ob+5)-cam_x),dy=(int16_t)(word(ob+8)-cam_y);
    if(dx>=8 && dx<=248 && dy>=0 && dy<224+32)
      arena|=(uint8_t)(1u<<other);
    break;
  }
  if(arena==3) {bounds.valid=0;return;}
  bounds.valid&=(uint8_t)~arena;
  /* Thirteen stage lists precede the first rectangle. The final list has
   * no following stage pointer; its end is that first rectangle's address. */
  unsigned data=rom_word(rom_word(0xe4e2));
  unsigned first=rom_word(0xe4e2+stage*2),end=stage<12?rom_word(0xe4e4+stage*2):data;
  if(data<0xe4fc || data>=0xecd0 || first<0xe4fc || end<first ||
      end>data || (end-first)%2) return;
  static const unsigned targets[4]={0x1e5e,0x1e60,0x1e68,0x1e6e};
  for(unsigned seat=0;seat<2;++seat) {
    if(arena&(1u<<seat)) continue;
    if(!living(&s->players[seat]) || s->players[seat].zero.swap_phase) {
      bounds.valid&=(uint8_t)~(1u<<seat);continue;
    }
    /* The world actor follows the native targets, scripted changes and all,
     * so a handoff gives him back exactly the room he was in. */
    if(seat==s->anchor || !(bounds.valid&(1u<<seat))) {
      for(unsigned i=0;i<4;++i) bounds.limits[seat][i]=(uint16_t)word(r+targets[i]);
      bounds.valid|=(uint8_t)(1u<<seat);
      if(seat==s->anchor) continue;
    }
    const uint8_t *body=seat==s->current?r+0xba8:s->players[seat].body;
    unsigned x=word(body+5),y=word(body+8);
    for(unsigned entry=first;entry<end;entry+=2) {
      unsigned record=rom_word(entry);
      if(record<data || record+8>=0xecd0 || x>=rom_word(record) ||
          x<rom_word(record+2) || y>=rom_word(record+4) || y<rom_word(record+6)) continue;
      /* Same exclusive upper edges and byte command list as F6E0..F71D.
       * Only authored camera limits are copied; no guest script runs twice. */
      for(unsigned n=8;n<40 && record+n<0xecd0;++n) {
        unsigned command=rom_byte(record+n);if(!command) break;
        unsigned address=rom_word(0xecd0+(command-1)*4);
        unsigned value=rom_word(0xecd2+(command-1)*4);
        if(value>8192) continue;
        for(unsigned i=0;i<4;++i) if(address==targets[i]) bounds.limits[seat][i]=(uint16_t)value;
      }
    }
  }
}
MmxCoopView MmxCoopViewWithBounds(const uint8_t *r,const MmxCoopState *s,unsigned seat,
                                 const MmxCoopViewBoundsState *b) {
  MmxCoopView v={word(r+0x1e4d),word(r+0x1e50),s->anchor};
  if(!online || !s->initialized || seat>1) return v;
  /* A transported partner watches the native script from its first frame,
   * including the teleport beams. Fallen/withdrawn seats watch the survivor. */
  if((s->scene_owner && !(s->scene_stay && seat!=s->anchor)) || s->stage_pending || r[0xd3]!=4)
    return v;
  const MmxCoopPlayer *p=&s->players[seat];
  /* A Select teleport keeps its own camera: the player watches his beam
   * leave, then follows it down beside the other player. */
  if(!living(p) || (p->zero.swap_phase && s->teleport_seat!=seat+1u)) seat=s->anchor;
  v.player=seat;
  if(seat==s->anchor) return v;
  const uint8_t *body=seat==s->current?r+0xba8:s->players[seat].body;
  /* Use the authored stage/room camera limits, not the other player's view.
   * X1's feet lie in the lower dead zone of the 224px native viewport. */
  int limits[4];player_limits(r,s,seat,b,limits);
  v.x=clamp(word(body+5)-128,limits[0],limits[1]);
  v.y=clamp(word(body+8)-160,limits[2],limits[3]);
  return v;
}
MmxCoopView MmxCoopViewForPlayer(const uint8_t *r,const MmxCoopState *s,unsigned seat) {
  return MmxCoopViewWithBounds(r,s,seat,&bounds);
}
bool MmxCoopViewsStageStart(unsigned stage,int *x,int *y,int limits[4]) {
  if(stage>12) return false;
  unsigned list=rom_word(0xa780+stage*2),record=0xa780+rom_word(0xa780+list);
  if(!rom || record<0xa780 || record+0x17>0xffff) return false;
  *x=(int)rom_word(record+3);*y=(int)rom_word(record+5);
  for(unsigned i=0;i<4;++i) limits[i]=(int)rom_word(record+0x0f+i*2);
  return *x<8192 && *y<8192 && limits[0]<=limits[1] && limits[2]<=limits[3] && limits[1]<=8192 && limits[3]<=8192;
}
void MmxCoopViewsSetSeatLimits(uint8_t *r,const MmxCoopState *s,unsigned seat,const int limits[4]) {
  if(!online || !s->initialized || seat>1) return;
  if(bounds.stage!=r[0x1f7a]) {memset(&bounds,0,sizeof(bounds));bounds.stage=r[0x1f7a];}
  for(unsigned i=0;i<4;++i) bounds.limits[seat][i]=(uint16_t)clamp(limits[i],0,8192);
  bounds.valid|=(uint8_t)(1u<<seat);
  if(seat!=s->anchor) return;
  static const unsigned targets[4]={0x1e5e,0x1e60,0x1e68,0x1e6e};
  for(unsigned i=0;i<4;++i) {
    putword(r+0x1e56+i*2,bounds.limits[seat][i]);putword(r+targets[i],bounds.limits[seat][i]);
  }
  const uint8_t *body=seat==s->current?r+0xba8:s->players[seat].body;
  int x=clamp(word(body+5)-128,limits[0],limits[1]),y=clamp(word(body+8)-160,limits[2],limits[3]);
  putword(r+0x1e4d,(unsigned)x);putword(r+0x1e6a,(unsigned)x);
  putword(r+0x1e50,(unsigned)y);putword(r+0x1e6c,(unsigned)y);
}
void MmxCoopViewsCopyBounds(const uint8_t *r,const MmxCoopState *s,unsigned to,unsigned from) {
  if(!online || !s->initialized || to>1 || from>1 || bounds.stage!=r[0x1f7a]) return;
  int limits[4];player_limits(r,s,from,&bounds,limits);
  for(unsigned i=0;i<4;++i) bounds.limits[to][i]=(uint16_t)clamp(limits[i],0,8192);
  bounds.valid|=(uint8_t)(1u<<to);
}
void MmxCoopViewsAdoptSeatView(uint8_t *r,const MmxCoopState *s,unsigned seat) {
  if(!online || !s->initialized || seat>1 || seat==s->anchor) return;
  const uint8_t *body=seat==s->current?r+0xba8:s->players[seat].body;
  int limits[4];player_limits(r,s,seat,&bounds,limits);
  int x=clamp(word(body+5)-128,limits[0],limits[1]),y=clamp(word(body+8)-160,limits[2],limits[3]);
  static const unsigned targets[4]={0x1e5e,0x1e60,0x1e68,0x1e6e};
  for(unsigned i=0;i<4;++i) {
    putword(r+0x1e56+i*2,(unsigned)clamp(limits[i],0,8192));
    putword(r+targets[i],(unsigned)clamp(limits[i],0,8192));
  }
  putword(r+0x1e4d,(unsigned)x);putword(r+0x1e6a,(unsigned)x);
  putword(r+0x1e50,(unsigned)y);putword(r+0x1e6c,(unsigned)y);
}
bool MmxCoopViewsAdoptPlayer(uint8_t *r,const MmxCoopState *s,unsigned seat) {
  if(!online || !s->initialized || seat>1 || seat==s->anchor || s->scene_owner ||
      s->stage_pending || r[0xd3]!=4 || bounds.stage!=r[0x1f7a] ||
      !(bounds.valid&(1u<<seat)) || !living(&s->players[seat]) ||
      s->players[seat].zero.swap_phase) return false;
  MmxCoopView v=MmxCoopViewForPlayer(r,s,seat);
  /* Sigma 1's upper door ($81:EC98) can be reached while the world actor
   * is still at the shaft bottom. Once the door freezes gameplay, native
   * scrolling must start from its entrant's view and room limits; keeping
   * the lower actor's camera leaves the door waiting forever. Copy both
   * current and target limits, plus the camera's previous-position pair,
   * before the caller changes the anchor and begins scene transport. */
  static const unsigned targets[4]={0x1e5e,0x1e60,0x1e68,0x1e6e};
  for(unsigned i=0;i<4;++i) {
    putword(r+0x1e56+i*2,bounds.limits[seat][i]);
    putword(r+targets[i],bounds.limits[seat][i]);
  }
  putword(r+0x1e4d,(unsigned)v.x);putword(r+0x1e6a,(unsigned)v.x);
  putword(r+0x1e50,(unsigned)v.y);putword(r+0x1e6c,(unsigned)v.y);
  return true;
}
bool MmxCoopViewSeatContains(const uint8_t *r,const MmxCoopState *s,unsigned seat,int x,int y,
                             int left,int right,int top,int bottom,int margin) {
  if(seat>1 || !living(&s->players[seat]) || s->players[seat].zero.swap_phase) return false;
  if(s->scene_owner && seat!=s->anchor) return false;
  MmxCoopView v=MmxCoopViewForPlayer(r,s,seat);
  int dx=(int16_t)(x-v.x),dy=(int16_t)(y-v.y);
  return dx>=-left-margin && dx<right+margin && dy>=-top && dy<bottom;
}
bool MmxCoopViewContains(const uint8_t *r,const MmxCoopState *s,int x,int y,
                         int left,int right,int top,int bottom,int margin) {
  for(unsigned seat=0;seat<2;++seat)
    if(MmxCoopViewSeatContains(r,s,seat,x,y,left,right,top,bottom,margin)) return true;
  return false;
}
