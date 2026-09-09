// Runs the production adapter with an in-memory engine double. This checks
// calls, ownership and transitions, NOT the Windows VM ABI or D3D8 rendering.
#include "../src/aim_policy.h"
#include "../src/coop_trio.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#ifdef __fastcall
#undef __fastcall
#endif
#define __fastcall
using BYTE=unsigned char; using DWORD=uint32_t; using BOOL=int;
const BOOL TRUE=1,FALSE=0;
struct FFrameLite {void *Object; BYTE *Code;};
using PFN_execActorFn=void (*)(void *,void *,FFrameLite *,void *);
struct TArrayLite {void **Data;int Num,Max;};
struct Fake {
    alignas(void *) BYTE data[512]={};
    void *cls=nullptr; std::string name; bool level=true,deleted=false;
    void *parts[2]={};
};
static std::vector<std::unique_ptr<Fake>> heap;
static void *slots[128]={};static TArrayLite objects={slots,0,128},*g_objArray=&objects;
static int cursorClass,gestureClass,spellClass,spell2Class,iconToken,icon2Token,wetToken;
static int changeFn,readyFn,targetFn,canFn,chooseFn;
static void *sIconProp=nullptr;
static void *g_clsAimFX=&cursorClass,*g_fnChoose=&chooseFn,*g_fnCanCast=&canFn;
static void *g_core=nullptr,*g_engine=nullptr;
static int g_propOffsetField=4,g_opByteConst=0;
static BOOL g_cgChainOK=TRUE,g_opsOK=TRUE,g_panePark=TRUE,g_bodyClear=TRUE;
static struct {BOOL ok;int Location;} F={TRUE,32};
static struct {int op;} g_ops[]={{0x23},{0x22},{0x16}};
enum {OP_VEC,OP_ROT,OP_END};
static DWORD clockMs=1;static int spawned=0,destroyed=0,changes=0,readies=0,targetings=0,choices=0;
static bool privateParts=true,placeOK=true,canCast=true,spawnOK=true;
static uint32_t lastIcon=0;static float lastRadius=0;
static void *g_aimFX[8]={},*g_cgBeginCls[8]={};
static BOOL g_nativeAimOwned[8]={},g_aimSup[8]={};
static DWORD g_aimFXAt[8]={};static float g_aimLast[8][3]={};
struct CgClass {int sizeOff=-1;};
struct CgCand {void *obj; CgClass *info; const char *name; void *spell;float radius;};
static CgCand *candidate=nullptr;
static Fake *cursorDefault,*gestureDefault,*spellDefault,*spell2Default;
static Fake *fake(void *p) {
    for(auto &f:heap) if(f.get()==p) return f.get();
    return nullptr;
}
static Fake *make(const char *name,void *cls=nullptr) {
    heap.emplace_back(new Fake);auto *f=heap.back().get();f->name=name;f->cls=cls;
    slots[objects.Num++]=f;return f;
}
static BOOL IsBadReadPtr(const void *p,size_t) {return p==nullptr;}
static char *objName(void *p,char *out,size_t n) {
    const char *name=p==&iconToken?"Texture HP_FX.SpellIcon1"
                  :p==&icon2Token?"Texture HP_FX.SpellIcon2"
                  :p==&wetToken?"WetTexture SpellShapes.SpellFX.SpongifyWet1"
                  :fake(p)?fake(p)->name.c_str():"Class hgame.Mock";
    std::snprintf(out,n,"%s",name);return out;
}
static void *cgClassOf(void *p) {return fake(p)?fake(p)->cls:nullptr;}
static BOOL cgIsKnownClass(void *p) {return p==&cursorClass||p==&gestureClass||p==&spellClass||p==&spell2Class;}
static void *cgSuper(void *) {return nullptr;}   // chain walk ends after the spell class
static BOOL actorInCurrentLevel(void *p) {return fake(p)&&fake(p)->level;}
static void *findObjectByPath(const char *path) {
    return sIconProp&&!std::strcmp(path,"hgame.Mock.SpellIcon")?sIconProp:nullptr;
}
static int propOffset(const char *) {return -1;}
static DWORD nativeBoolBitMask(const char *) {return 0;}
static void *GetProcAddress(void *,const char *) {return nullptr;}
static void *findOrigCursor() {return nullptr;}
static DWORD GetTickCount() {return clockMs;}
static void logf_(const char *,...) {}
static void cgBuildIndex() {}
static CgCand *cgPickTarget(int,void *,float *,BOOL,float *) {return candidate;}
static void *cgSpellClassFor(CgCand *c,void *) {return c->spell;}
static void cgAimPoint(CgCand *c,float out[3]) {std::memcpy(out,(BYTE *)c->obj+F.Location,12);}
static float cgHitRadius(CgCand *c) {return c->radius;}
static BOOL fastTraceClear(void *,const float *,const float *) {return TRUE;}
static BOOL nativeSetPhysics(void *,int physics) {assert(physics==0);return TRUE;}
static void setBoolProp(void *o,int off,DWORD mask,BOOL on) {
    DWORD *v=(DWORD *)((BYTE *)o+(off&~3));if(on)*v|=mask;else *v&=~mask;
}
static void location(void *o,void *,FFrameLite *f,void *result) {
    assert(f->Object==o&&f->Code[0]==0x23&&f->Code[13]==0x16);
    std::memcpy((BYTE *)o+F.Location,f->Code+1,12);*(DWORD *)result=placeOK;
}
static void rotation(void *,void *,FFrameLite *f,void *) {assert(f->Code[0]==0x22);}
static void *defaults(void *cls,void *) {
    return cls==&cursorClass?cursorDefault:cls==&gestureClass?gestureDefault:cls==&spellClass?spellDefault:spell2Default;
}
static void particles(Fake *f,int count,bool unique) {
    for(int k=0;k<count;k++) f->parts[k]=unique?make("SpriteEmitter Save0.Private"):cursorDefault->parts[k];
    TArrayLite *arr=(TArrayLite *)(f->data+160);arr->Data=f->parts;arr->Num=count;arr->Max=count;
}
static void *spawnFX(void *,void *cls,void *,const float *,const int *) {
    if(!spawnOK) return nullptr;
    ++spawned;auto *f=make(("Emitter Save0.FX"+std::to_string(spawned)).c_str(),cls);
    particles(f,cls==&gestureClass?2:1,privateParts);return f;
}
static void event(void *o,void *,void *fn,void *params,void *) {
    if(fn==&changeFn) {changes++;std::memcpy(&lastIcon,params,4);}
    else if(fn==&readyFn) {readies++;lastRadius=*(float *)((BYTE *)params+4);setBoolProp(o,112,4,!(*(DWORD *)params&1));}
    else if(fn==&targetFn) {targetings++;assert(*(DWORD *)params==1);}
    else if(fn==&canFn) *(DWORD *)params=canCast;
    else assert(false);
}
static void callFnP(void *,void *,void *,int,const char *) {choices++;}
static auto g_ProcessEvent=event;
static auto g_execSetPhysics=location,g_execSpawn=location,g_execDestroy=location;
static BOOL destroyAimFX(int);
#include "../src/native_aim.h"
static BOOL destroyAimFX(int i) {
    if(g_aimFX[i] && (!g_nativeAimOwned[i]||nativeAimOwnedAlive(i))) {
        fake(g_aimFX[i])->deleted=true;destroyed++;
    }
    g_aimFX[i]=nullptr;nativeAimForget(i);return TRUE;
}
static bool hidden(int i) {return (*(DWORD *)((BYTE *)g_aimFX[i]+112)&4)!=0;}
static void setup() {
    nativeAimResetBindings();
    g_naB.tried=g_naB.ok=TRUE;g_naB.getDefault=defaults;
    g_naB.gestureClass=&gestureClass;g_naB.cursorClass=&cursorClass;
    g_naB.deleted=116;g_naB.deletedMask=8;
    g_naB.emitters=160;g_naB.icon=80;g_naB.hidden=112;g_naB.hiddenMask=4;
    g_naB.change=&changeFn;g_naB.ready=&readyFn;g_naB.targeting=&targetFn;
    g_naB.readyMask=g_naB.targetingMask=1;g_naB.radiusParam=4;
    g_naB.gestureDistance=-1;g_naB.canCastMask=1;
    g_naB.setLocation=location;g_naB.setRotation=rotation;
}
int main() {
    cursorDefault=make("Emitter hgame.CursorDefault");particles(cursorDefault,1,true);
    gestureDefault=make("Emitter hgame.GestureDefault");particles(gestureDefault,2,true);
    spellDefault=make("Spell hgame.SpellDefault");*(void **)(spellDefault->data+80)=&iconToken;
    spell2Default=make("Spell hgame.Spell2Default");*(void **)(spell2Default->data+80)=&icon2Token;
    auto *pawn=make("hermione Save0.Hermione1");auto *target=make("Trigger Save0.Pad1");
    float centre[3]={400,0,0};std::memcpy(target->data+F.Location,centre,12);
    float cam[3]={-125,0,50};int rot[3]={0,0,0};
    CgClass info;CgCand c={target,&info,"Pad1",&spellClass,60};
    setup();
    assert(updateNativeAim(1,pawn,TRUE,cam,rot));assert(spawned==1&&targetings==1&&!hidden(1));
    assert(updateNativeAim(1,pawn,TRUE,cam,rot));assert(spawned==1&&targetings==1);
    nativeAimPaneVisible(0);assert(hidden(1));nativeAimPaneVisible(1);assert(!hidden(1));
    assert(std::fabs(((float *)(fake(g_aimFX[1])->data+F.Location))[0]-770)<0.01f);
    candidate=&c;assert(updateNativeAim(1,pawn,TRUE,cam,rot));
    assert(spawned==2&&destroyed==1&&changes==1&&readies==1&&choices==1);
    assert(lastIcon==uint32_t(uintptr_t(&iconToken))&&lastRadius==60&&!hidden(1));
    // v64: the LOCK gesture snaps once at lock (stock SetLocation), then
    // chases the moved goal with MoveSmooth((goal-loc)*10*dt) - it eases,
    // never teleports, and converges.
    {
        float *locP=(float *)(fake(g_aimFX[1])->data+F.Location);
        assert(std::fabs(locP[0]-(400.0f-68.0f))<0.01f);     // snap at lock
        int newRot[3]={0,4096,0};
        clockMs+=16;
        assert(updateNativeAim(1,pawn,TRUE,cam,newRot));
        float dir2[3];hp3aim::direction(newRot,dir2);
        float g2[3]={400.0f-dir2[0]*68.0f,-dir2[1]*68.0f,-dir2[2]*68.0f};
        // one frame moved partway toward the new goal, not all the way
        assert(locP[0]>332.0f&&locP[0]<g2[0]+0.01f);
        for(int f=0;f<200;f++){clockMs+=16;updateNativeAim(1,pawn,TRUE,cam,newRot);}
        for(int j=0;j<3;j++) assert(std::fabs(locP[j]-g2[j])<0.05f);
    }
    c.radius=120;assert(updateNativeAim(1,pawn,TRUE,cam,rot));assert(spawned==2&&readies==2&&lastRadius==120);
    canCast=false;updateNativeAim(1,pawn,TRUE,cam,rot);nativeAimPaneVisible(1);assert(hidden(1));
    canCast=true;updateNativeAim(1,pawn,TRUE,cam,rot);assert(!hidden(1));
    c.spell=&spell2Class;updateNativeAim(1,pawn,TRUE,cam,rot);
    assert(spawned==3&&changes==2&&lastIcon==uint32_t(uintptr_t(&icon2Token)));
    // P2 and P3 each own a separate actor; masking one never moves the other.
    updateNativeAim(2,pawn,TRUE,cam,rot);assert(g_aimFX[1]!=g_aimFX[2]);
    nativeAimPaneVisible(1);assert(!hidden(1)&&hidden(2));nativeAimPaneVisible(2);assert(hidden(1)&&!hidden(2));
    updateNativeAim(1,pawn,FALSE,cam,rot);assert(!g_aimFX[1]&&!g_nativeAimOwned[1]);
    // A recycled object-table slot is not ours to write to or destroy.
    slots[g_na[2].slot]=pawn;int oldDestroyed=destroyed;
    destroyAimFX(2);assert(destroyed==oldDestroyed&&!g_aimFX[2]);
    candidate=nullptr;privateParts=false;int oldSpawned=spawned;
    updateNativeAim(1,pawn,TRUE,cam,rot);assert(hidden(1));
    clockMs+=1100;assert(!updateNativeAim(1,pawn,TRUE,cam,rot));assert(g_naFailed[1]&&!g_aimFX[1]);
    assert(!updateNativeAim(1,pawn,TRUE,cam,rot));assert(spawned==oldSpawned+1);
    updateNativeAim(1,pawn,FALSE,cam,rot);privateParts=true;
    assert(updateNativeAim(1,pawn,TRUE,cam,rot));assert(!g_naFailed[1]);
    setBoolProp(g_aimFX[1],116,8,TRUE);assert(!nativeAimOwnedAlive(1));
    setBoolProp(g_aimFX[1],116,8,FALSE);assert(nativeAimOwnedAlive(1));
    fake(g_aimFX[1])->level=false;assert(!nativeAimOwnedAlive(1));destroyAimFX(1);
    placeOK=false;assert(!updateNativeAim(1,pawn,TRUE,cam,rot));assert(!g_aimFX[1]);
    updateNativeAim(1,pawn,FALSE,cam,rot);placeOK=true;spawnOK=false;
    assert(!updateNativeAim(1,pawn,TRUE,cam,rot));assert(g_naFailed[1]);
    nativeAimResetBindings();assert(!g_naFailed[1]&&!g_naB.tried);
    // ---- v53: the gesture icon resolves on the spell's own class chain ----
    // A SpellIcon declared on the spell class shadows the base anchor offset,
    // wet textures are legal glyphs, and a non-material value is refused
    // (with a diagnostic) instead of being handed to ChangeGesture.
    spawnOK=true;placeOK=true;privateParts=true;canCast=true;candidate=nullptr;
    Fake *iconProp=make("ObjectProperty hgame.Mock.SpellIcon");
    g_propOffsetField=0;        // the fake object's data block is its head
    *(int *)(iconProp->data)=88;                         // UProperty::Offset value
    *(void **)(spellDefault->data+80)=nullptr;           // base anchor slot: empty
    *(void **)(spell2Default->data+80)=nullptr;
    *(void **)(spellDefault->data+88)=&iconToken;        // declared on the class chain
    *(void **)(spell2Default->data+88)=&wetToken;
    sIconProp=iconProp;
    setup();                                             // re-arms bindings (anchor 80)
    updateNativeAim(1,pawn,FALSE,cam,rot);
    c.spell=&spellClass;c.radius=60;candidate=&c;
    int ch=changes;
    assert(updateNativeAim(1,pawn,TRUE,cam,rot));        // chain offset won over the anchor
    assert(changes==ch+1&&lastIcon==uint32_t(uintptr_t(&iconToken)));
    c.spell=&spell2Class;
    assert(updateNativeAim(1,pawn,TRUE,cam,rot));        // wet-texture glyph accepted
    assert(changes==ch+2&&lastIcon==uint32_t(uintptr_t(&wetToken)));
    *(void **)(spell2Default->data+88)=&gestureClass;    // a class is not a material
    assert(!updateNativeAim(1,pawn,TRUE,cam,rot));
    assert(g_naFailed[1]&&!g_aimFX[1]);                  // refused + latched, nothing drawn
    *(void **)(spell2Default->data+88)=&wetToken;
    updateNativeAim(1,pawn,FALSE,cam,rot);               // release clears the latch
    assert(updateNativeAim(1,pawn,TRUE,cam,rot));
    assert(changes==ch+3&&lastIcon==uint32_t(uintptr_t(&wetToken)));
    sIconProp=nullptr;
    puts("native aim adapter: seeking, lock, icon/radius/readiness, pane isolation, cleanup and fallback passed");
}
