// Included once by dllmain.cpp after the v51 candidate/FX helpers.
// HP3 v129 package evidence and deviations: docs/NATIVE_AIM.md.
// This is a visual adapter, NOT a second SpellCursor/controller: calling the
// full cursor lockOn would drive P1-only controller logic and gameplay events.
// v53: the gesture icon is resolved the way the class system defines it -
// <spellClass>.default.SpellIcon with the property located on the spell's own
// class chain, and any renderable material accepted (HP's gesture glyphs are
// WetTexture/Shader, which the old strict "Texture " name check rejected, so
// the locked gesture never spawned and the legacy sparkle marker drew over
// the native effect on hardware).
struct NativeAimBinding {
    BOOL tried, ok;
    void *gestureClass, *cursorClass, *cursorDefault;
    void *change, *ready, *targeting;
    int textureParam, readyParam, radiusParam, targetingParam;
    DWORD readyMask, targetingMask;
    int emitters, icon, hidden, range, gestureDistance;
    int canCastReturn; DWORD canCastMask;
    DWORD hiddenMask, deletedMask;
    int deleted;
    PFN_execActorFn setLocation, setRotation;
    typedef void *(__fastcall *DefaultObject)(void *, void *);
    DefaultObject getDefault;
};
static NativeAimBinding g_naB = {};
struct NativeAimState {
    int slot;
    void *fxClass, *pawn, *target, *spell, *icon;
    char name[160];
    BOOL gesture, configured, visible, ready;
    float radius;
    DWORD spawnedAt, retryAt;
};
static NativeAimState g_na[8] = {};
static BOOL g_naFailed[8] = {0}; // a failed renderer stays legacy until next hold

static int naField(const char *path, const char *type) {
    void *p=findObjectByPath(path); char name[240];
    if (!p || strncmp(objName(p,name,sizeof(name)),type,strlen(type)) || name[strlen(type)]!=' ') return -1;
    return propOffset(path);
}
static void *naFunction(const char *path) {
    void *fn=findObjectByPath(path); char name[240];
    return fn && !strncmp(objName(fn,name,sizeof(name)),"Function ",9) ? fn : NULL;
}
static void naDefaultBinding(void) {
    if (!g_naB.getDefault && g_core)
        g_naB.getDefault=(NativeAimBinding::DefaultObject)GetProcAddress(g_core,"?GetDefaultObject@UClass@@QAEPAVUObject@@XZ");
    if (!g_naB.cursorClass) g_naB.cursorClass=findObjectByPath("hgame.SpellCursor");
    if (!g_naB.cursorDefault && g_naB.getDefault && g_naB.cursorClass)
        g_naB.cursorDefault=g_naB.getDefault(g_naB.cursorClass,NULL);
}
static float nativeAimRange(void) {
    if (!F.ok) return 770.0f;
    naDefaultBinding();
    int off=g_naB.range;
    if (off<=0) g_naB.range=off=naField("KWGame.SelectCursor.fLOS_Distance","FloatProperty");
    void *cur=findOrigCursor();
    if (!cur) cur=g_naB.cursorDefault;
    if (cur && off>0 && !IsBadReadPtr((BYTE *)cur+off,4))
        return hp3aim::range(*(float *)((BYTE *)cur+off));
    return 770.0f; // verified HP3 SpellCursor default, not the KW base 1500
}
static BOOL naBind(void) {
    if (g_naB.tried) return g_naB.ok;
    if (!F.ok || g_propOffsetField<0 || !g_cgChainOK) return FALSE;
    g_naB.tried=TRUE; naDefaultBinding();
    g_naB.gestureClass=findObjectByPath("hgame.SpellGesture");
    g_naB.change=naFunction("hgame.SpellGesture.ChangeGesture");
    g_naB.ready=naFunction("hgame.SpellGesture.SetReadyToCast");
    g_naB.targeting=naFunction("hgame.SpellCursorEmitter.DoTargeting");
    g_naB.textureParam=naField("hgame.SpellGesture.ChangeGesture.NewTexture","ObjectProperty");
    g_naB.readyParam=naField("hgame.SpellGesture.SetReadyToCast.bReady","BoolProperty");
    g_naB.radiusParam=naField("hgame.SpellGesture.SetReadyToCast.Radius","FloatProperty");
    g_naB.targetingParam=naField("hgame.SpellCursorEmitter.DoTargeting.bDo","BoolProperty");
    g_naB.readyMask=nativeBoolBitMask("hgame.SpellGesture.SetReadyToCast.bReady");
    g_naB.targetingMask=nativeBoolBitMask("hgame.SpellCursorEmitter.DoTargeting.bDo");
    g_naB.emitters=naField("Engine.Emitter.Emitters","ArrayProperty");
    g_naB.icon=naField("hgame.baseSpell.SpellIcon","ObjectProperty");
    g_naB.hidden=naField("Engine.Actor.bHidden","BoolProperty");
    g_naB.hiddenMask=nativeBoolBitMask("Engine.Actor.bHidden");
    g_naB.deleted=naField("Engine.Actor.bDeleteMe","BoolProperty");
    g_naB.deletedMask=nativeBoolBitMask("Engine.Actor.bDeleteMe");
    g_naB.canCastReturn=naField("hgame.HPCharacter.canCast.ReturnValue","BoolProperty");
    g_naB.canCastMask=nativeBoolBitMask("hgame.HPCharacter.canCast.ReturnValue");
    g_naB.gestureDistance=naField("Engine.Actor.GestureDistance","FloatProperty");
    g_naB.setLocation=(PFN_execActorFn)GetProcAddress(g_engine,"?execSetLocation@AActor@@QAEXAAUFFrame@@QAX@Z");
    g_naB.setRotation=(PFN_execActorFn)GetProcAddress(g_engine,"?execSetRotation@AActor@@QAEXAAUFFrame@@QAX@Z");
    // These exact functions/parameters were decoded from the supplied HP3
    // packages. Refuse a different ABI; do not guess where to write a param.
    g_naB.ok=g_naB.getDefault && g_naB.gestureClass && g_clsAimFX &&
        g_naB.change && g_naB.ready && g_naB.targeting &&
        g_naB.textureParam==0 && g_naB.readyParam==0 && g_naB.radiusParam==4 &&
        g_naB.targetingParam==0 && g_naB.readyMask==1 && g_naB.targetingMask==1 &&
        g_naB.emitters>0 && g_naB.icon>0 && g_naB.hidden>0 && g_naB.hiddenMask &&
        g_naB.setLocation && g_naB.setRotation && g_execSetPhysics && g_opByteConst>=0 &&
        g_ProcessEvent && g_execSpawn && g_execDestroy && g_opsOK &&
        g_naB.deleted>0 && g_naB.deletedMask && cgIsKnownClass(g_naB.gestureClass) &&
        cgIsKnownClass(g_clsAimFX);
    logf_("[nativeaim] bindings %s: SpellGesture=%p Emitters=+0x%X SpellIcon=+0x%X "
          "params=%d/%d/%d/%d range=%.0f SetLocation=%p hidden=+0x%X/0x%lX delete=+0x%X/0x%lX",g_naB.ok?"OK":"UNAVAILABLE (legacy sprite fallback)",
          g_naB.gestureClass,g_naB.emitters,g_naB.icon,g_naB.textureParam,g_naB.readyParam,
          g_naB.radiusParam,g_naB.targetingParam,nativeAimRange(),(void *)g_naB.setLocation,
          g_naB.hidden,(unsigned long)g_naB.hiddenMask,g_naB.deleted,(unsigned long)g_naB.deletedMask);
    return g_naB.ok;
}
static void nativeAimForget(int i) {
    if (i<0 || i>=8) return;
    memset(&g_na[i],0,sizeof(g_na[i])); g_nativeAimOwned[i]=FALSE;
}
static BOOL nativeAimOwnedAlive(int i) {
    if (i<0 || i>=8 || !g_nativeAimOwned[i] || !g_aimFX[i] || !g_objArray) return FALSE;
    NativeAimState &s=g_na[i];
    if (s.slot<0 || s.slot>=g_objArray->Num || IsBadReadPtr(g_objArray->Data+s.slot,4)) return FALSE;
    void *o=g_objArray->Data[s.slot];
    if (o!=g_aimFX[i] || IsBadReadPtr(o,0x2C)) return FALSE;
    char name[160]; objName(o,name,sizeof(name));
    return hp3aim::sameIdentity(g_aimFX[i],o,s.fxClass,cgClassOf(o),s.name,name) &&
           actorInCurrentLevel(o) && g_naB.deleted>0 && g_naB.deletedMask &&
           !IsBadReadPtr((BYTE *)o+(g_naB.deleted&~3),4) &&
           !(*(DWORD *)((BYTE *)o+(g_naB.deleted&~3)) & g_naB.deletedMask);
}
static BOOL naPlace(void *fx, const float at[3], const int rot[3]) {
    BYTE bc[16]; FFrameLite frame; memset(&frame,0,sizeof(frame));
    frame.Object=fx; frame.Code=bc; DWORD result=0;
    bc[0]=(BYTE)g_ops[OP_VEC].op; memcpy(bc+1,at,12); bc[13]=(BYTE)g_ops[OP_END].op;
    bc[14]=bc[13]; g_naB.setLocation(fx,NULL,&frame,&result);
    if (!result) return FALSE;
    frame.Code=bc; bc[0]=(BYTE)g_ops[OP_ROT].op; memcpy(bc+1,rot,12);
    g_naB.setRotation(fx,NULL,&frame,&result);
    return TRUE;
}
// Spawn must have INSTANCED particles, not shared CDO archetypes. This also
// catches the zero-Emitters failure from the old hardware logs before scripts
// index Emitters[0]/[1]. ParticleEmitter is a UObject, never an Actor.
static BOOL naParticles(void *fx, void *cls, int required) {
    int off=g_naB.emitters;
    if (IsBadReadPtr((BYTE *)fx+off,12)) return FALSE;
    TArrayLite *a=(TArrayLite *)((BYTE *)fx+off);
    if (a->Num<required || a->Num>16 || a->Max<a->Num || a->Max>1024 || IsBadReadPtr(a->Data,a->Num*4)) return FALSE;
    void *def=g_naB.getDefault(cls,NULL);
    TArrayLite *d=def && !IsBadReadPtr((BYTE *)def+off,12) ? (TArrayLite *)((BYTE *)def+off) : NULL;
    if (!d || d->Num<required || d->Num>16 || d->Max<d->Num || d->Max>1024 || IsBadReadPtr(d->Data,d->Num*4) || a->Data==d->Data) return FALSE;
    for(int k=0;k<required;k++) {
        void *part=a->Data[k]; char name[200];
        if (!part || IsBadReadPtr(part,0x2C) || part==d->Data[k] ||
            strncmp(objName(part,name,sizeof(name)),"SpriteEmitter ",14)) return FALSE;
        for(int j=0;j<d->Num;j++) if(part==d->Data[j]) return FALSE;
    }
    return TRUE;
}
// ---------------------------------------------------------------------------
// Gesture icon resolution (v53).
//
// The stock cursor hands SpellGesture.ChangeGesture the value of
// <spellClass>.default.SpellIcon. v52 hardcoded where that property lives
// (hgame.baseSpell, ObjectProperty) and what it holds ("Texture "). On the
// retail packages the hardware log then showed "no readable default.SpellIcon"
// for every Depulso/Spongify target: the locked SpellGesture never spawned and
// the legacy Sparkle_1/3/7 sprite drew over the native seek effect instead.
// Resolve it structurally instead of by assumption:
//   1. find the "SpellIcon" property on the spell class's OWN chain (a
//      subclass may shadow the base declaration, which moves the offset);
//   2. accept any material-family object the slot holds - HP gesture glyphs
//      are WetTexture in the sibling KW games, and ChangeGesture is called
//      with exactly this object by the stock scripts;
//   3. if it still cannot be read, log precisely what sits at the slot so a
//      hardware log is conclusive (None / wrong class / unreadable) instead
//      of falling back silently.
// ---------------------------------------------------------------------------
struct NaIconCache { void *cls; int off; BOOL done; };
static NaIconCache g_naIcon[16] = {};

static BOOL naMaterialToken(const char *name) {
    // Renderable material classes the gesture sprite's Texture may legally
    // hold. Anything else (Class/Sound/Actor/Emitter names) is refused.
    static const char *const kMat[] = {
        "Texture ", "WetTexture ", "ScriptedTexture ", "Cubemap ",
        "Shader ", "Modifier ", "FinalBlend ", "Combiner ",
        "Material ", "RenderedMaterial ", "BitmapMaterial ",
        "ShadowBitmapTexture ", NULL };
    for (int k = 0; kMat[k]; k++)
        if (!strncmp(name, kMat[k], strlen(kMat[k]))) return TRUE;
    return FALSE;
}
static int naIconLookup(const char *clsPath) {
    size_t n = strlen(clsPath);
    if (n < 4 || n > 200) return -1;
    char path[224];
    snprintf(path, sizeof(path), "%s.SpellIcon", clsPath);
    void *p = findObjectByPath(path);
    if (!p) return -1;
    char nb[240];
    if (strncmp(objName(p, nb, sizeof(nb)), "ObjectProperty ", 15)) return -1;
    if (g_propOffsetField < 0 || IsBadReadPtr((BYTE *)p + g_propOffsetField, 4)) return -1;
    int off = (int)*(DWORD *)((BYTE *)p + g_propOffsetField);
    return (off > 0 && off <= 0x4000) ? off : -1;
}
static int naIconOffset(void *spell) {
    if (!spell) return g_naB.icon;
    for (int k = 0; k < 16; k++)
        if (g_naIcon[k].done && g_naIcon[k].cls == spell) return g_naIcon[k].off;
    int slot = -1;
    for (int k = 0; k < 16 && slot < 0; k++) if (!g_naIcon[k].cls) slot = k;
    if (slot < 0) slot = (int)(((size_t)spell >> 4) % 16);
    int off = -1;
    void *c = spell;
    int depth = 0;
    while (c && cgIsKnownClass(c) && depth++ < 32) {   // most-derived first
        char cn[240];
        if (!objName(c, cn, sizeof(cn))[0]) break;
        const char *sp = strchr(cn, ' ');
        if (!sp || !sp[1]) break;
        off = naIconLookup(sp + 1);
        if (off > 0) break;
        c = cgSuper(c);
    }
    if (off <= 0) off = g_naB.icon;   // research anchor: hgame.baseSpell.SpellIcon
    g_naIcon[slot].cls = spell; g_naIcon[slot].off = off; g_naIcon[slot].done = TRUE;
    return off;
}
static void naIconDiag(void *spell, char *out, int cap) {
    // e.g. "hgame.DepulsoSpell SpellIcon(+0x42C)=None"
    out[0] = 0;
    if (!spell) { snprintf(out, cap, "spell class unresolved"); return; }
    char sn[160];
    objName(spell, sn, sizeof(sn));
    const char *cls = strchr(sn, ' ');
    cls = cls ? cls + 1 : sn;
    if (!g_naB.getDefault) { snprintf(out, cap, "%s (no GetDefaultObject binding)", cls); return; }
    void *def = g_naB.getDefault(spell, NULL);
    if (!def) { snprintf(out, cap, "%s (no default object)", cls); return; }
    int off = naIconOffset(spell);
    if (off <= 0 || IsBadReadPtr((BYTE *)def + off, 4)) {
        snprintf(out, cap, "%s (no SpellIcon property on its class chain)", cls);
        return;
    }
    void *v = *(void **)((BYTE *)def + off);
    if (!v) { snprintf(out, cap, "%s SpellIcon(+0x%X)=None", cls, off); return; }
    if (IsBadReadPtr(v, 0x2C)) { snprintf(out, cap, "%s SpellIcon(+0x%X)=unreadable %p", cls, off, v); return; }
    char vb[200];
    snprintf(out, cap, "%s SpellIcon(+0x%X)=%s", cls, off, objName(v, vb, sizeof(vb)));
}
static void *naSpellIcon(void *spell) {
    if (!cgIsKnownClass(spell) || !g_naB.getDefault) return NULL;
    void *def = g_naB.getDefault(spell, NULL);
    if (!def) return NULL;
    int off = naIconOffset(spell);
    if (off <= 0 || IsBadReadPtr((BYTE *)def + off, 4)) return NULL;
    void *icon = *(void **)((BYTE *)def + off);
    char name[200];
    // Whatever ChangeGesture would be handed by the stock cursor: a
    // renderable material, not an arbitrary actor/class/FX reference.
    if (!icon || IsBadReadPtr(icon, 0x2C) || !naMaterialToken(objName(icon, name, sizeof(name))))
        return NULL;
    return icon;
}
static void naVisible(int i, BOOL visible) {
    if (!nativeAimOwnedAlive(i)) return;
    setBoolProp(g_aimFX[i],g_naB.hidden,g_naB.hiddenMask,!visible);
}
static void nativeAimPaneVisible(int pane) {
    for(int i=1;i<8;i++) if(g_nativeAimOwned[i])
        naVisible(i,g_na[i].visible && (!g_panePark || pane==i));
    // Native particles keep their real world position between game ticks.
    // Unlike the legacy sprite path, never move their source into the void.
}
static void nativeAimResetBindings(void) {
    memset(&g_naB,0,sizeof(g_naB));
    memset(g_naFailed,0,sizeof(g_naFailed));
    memset(g_naIcon,0,sizeof(g_naIcon)); // spell-class pointers may be recycled
}
static BOOL naFallback(int i) {
    if(g_nativeAimOwned[i]) destroyAimFX(i);
    g_naFailed[i]=TRUE;
    return FALSE;
}
static BOOL updateNativeAim(int i, void *pawn, BOOL held, const float cam[3], const int rot[3]) {
    if(i<1 || i>=8) return FALSE;
    if (!held || !pawn || !actorInCurrentLevel(pawn)) {
        destroyAimFX(i); g_naFailed[i]=FALSE; g_aimSup[i]=FALSE; return TRUE;
    }
    if (g_naFailed[i]) return FALSE;
    cgBuildIndex();
    if (!naBind()) return FALSE;
    CgCand *c=cgPickTarget(i,pawn,NULL,FALSE,NULL);
    void *spell=c ? cgSpellClassFor(c,NULL) : NULL;
    void *icon=spell ? naSpellIcon(spell) : NULL;
    // No fabricated spell/color mapping: a missing icon is diagnosed (exact
    // class, slot offset and value) and the legacy marker remains available
    // instead of showing the wrong symbol.
    if (c && !icon) {
        if (!g_na[i].retryAt || GetTickCount()-g_na[i].retryAt>3000) {
            char diag[240];
            naIconDiag(spell,diag,sizeof(diag));
            logf_("[nativeaim] p%d target %s has no readable default.SpellIcon (%s); legacy marker",i+1,c->name,diag);
            g_na[i].retryAt=GetTickCount();
        }
        return naFallback(i);
    }
    float dir[3], at[3]; hp3aim::direction(rot,dir);
    float *pl=(float *)((BYTE *)pawn+F.Location);
    float length=hp3aim::rayLength(cam,pl,dir,nativeAimRange());
    BOOL gesture=c && icon;
    BOOL ready=TRUE;
    // Same pawn query as HPHeroController.canCast, but on THIS player. Never
    // invoke P1's controller or change the working v51 cast authorization.
    if(gesture && g_fnCanCast && g_naB.canCastReturn==0 && g_naB.canCastMask==1) {
        BYTE params[64]={0};
        g_ProcessEvent(pawn,NULL,g_fnCanCast,params,NULL);
        ready=(*(DWORD *)params & g_naB.canCastMask)!=0;
    }
    float radius=c?cgHitRadius(c):0;
    if (gesture) {
        float centre[3]; cgAimPoint(c,centre);
        float sm=1,gd=0;
        if(c->info && c->info->sizeOff>0 && !IsBadReadPtr((BYTE *)c->obj+c->info->sizeOff,4))
            sm=*(float *)((BYTE *)c->obj+c->info->sizeOff);
        if(!std::isfinite(sm) || sm<0 || sm>20) sm=1;
        if(g_naB.gestureDistance>0 && !IsBadReadPtr((BYTE *)c->obj+g_naB.gestureDistance,4))
            gd=*(float *)((BYTE *)c->obj+g_naB.gestureDistance);
        if(!std::isfinite(gd) || fabsf(gd)>2000) gd=0;
        hp3aim::gesturePoint(centre,dir,hp3aim::gestureDistance(cgHitRadius(c),sm,gd),at);
    } else {
        // FastTrace surface probe on the camera ray. The stock script uses
        // TraceActors; the existing mod uses FastTrace (world geometry only).
        // Binary refinement, rather than v51's 3000-unit stepped overshoot.
        for(int k=0;k<3;k++) at[k]=cam[k]+dir[k]*length;
        if (!fastTraceClear(pawn,cam,at)) {
            float low=0,high=length;
            for(int step=0;step<12;step++) {
                float mid=(low+high)*0.5f;
                for(int k=0;k<3;k++) at[k]=cam[k]+dir[k]*mid;
                if(fastTraceClear(pawn,cam,at)) low=mid; else high=mid;
            }
            length=std::fmax(0.0f,low-4.0f);
            for(int k=0;k<3;k++) at[k]=cam[k]+dir[k]*length;
        }
    }
    NativeAimState &s=g_na[i];
    void *cls=gesture?g_naB.gestureClass:g_clsAimFX;
    void *target=c?c->obj:NULL;
    BOOL changed=!g_nativeAimOwned[i] || !nativeAimOwnedAlive(i) || s.pawn!=pawn ||
                 s.target!=target || s.spell!=spell || s.icon!=icon || s.gesture!=gesture;
    if(changed) {
        destroyAimFX(i);
        // Unlike P1, whose emitter trails the hidden cursor actor, ours is
        // driven directly. PHYS_Trailer would glue the source to the pawn.
        int initialRot[3]={-rot[0],(rot[1]+32768)&65535,0};
        g_aimFX[i]=spawnFX(pawn,cls,pawn,at,gesture?initialRot:rot);
        if (!g_aimFX[i]) { logf_("[nativeaim] p%d native effect spawn failed; legacy marker until release",i+1); return naFallback(i); }
        s.pawn=pawn;s.target=target;s.spell=spell;s.icon=icon;s.gesture=gesture;
        s.fxClass=cgClassOf(g_aimFX[i]);s.slot=-1;s.spawnedAt=GetTickCount();
        objName(g_aimFX[i],s.name,sizeof(s.name));
        for(int slot=0;slot<g_objArray->Num;slot++) if(g_objArray->Data[slot]==g_aimFX[i]) {s.slot=slot;break;}
        g_nativeAimOwned[i]=TRUE;g_aimFXAt[i]=s.spawnedAt;
        if(!nativeAimOwnedAlive(i) || !nativeSetPhysics(g_aimFX[i],0)) {return naFallback(i);}
    }
    int facing[3]={-rot[0],(rot[1]+32768)&65535,0}; // gesture faces -vLOS_Dir
    BOOL placed=naPlace(g_aimFX[i],at,gesture?facing:rot);
    if(!placed) {logf_("[nativeaim] p%d native SetLocation failed; legacy marker until release",i+1);return naFallback(i);}
    memcpy(g_aimLast[i],at,12);
    if(!s.configured) {
        if(!naParticles(g_aimFX[i],cls,gesture?2:1)) {
            naVisible(i,FALSE);
            // Some engine builds initialize the particle copies at first tick.
            if(GetTickCount()-s.spawnedAt<1000) return TRUE;
            logf_("[nativeaim] p%d %s lacks private particle instances after 1s; legacy marker",i+1,s.name);
            return naFallback(i);
        }
        BYTE params[64]={0};
        if(gesture) {
            memcpy(params+g_naB.textureParam,&icon,4);
            g_ProcessEvent(g_aimFX[i],NULL,g_naB.change,params,NULL);
            // ChooseSpell on hover as well as at begin/release; no StartCasting
            // restart, no full SpellCursor.lockOn, no P1 controller substitution.
            if(spell!=g_cgBeginCls[i] && g_fnChoose) {
                memset(params,0,sizeof(params));memcpy(params,&spell,4);*(DWORD *)(params+4)=1;
                callFnP(pawn,g_fnChoose,params,12,"ChooseSpell(hoverCls,force)");
                g_cgBeginCls[i]=spell;
            }
        } else {
            *(DWORD *)(params+g_naB.targetingParam)=g_naB.targetingMask;
            g_ProcessEvent(g_aimFX[i],NULL,g_naB.targeting,params,NULL);
        }
        s.configured=TRUE; s.radius=-1;
        char iconName[200];
        logf_("[nativeaim] p%d %s: target=%s icon=%s radius=%.1f range=%.0f privateParticles=%d physics=None",
              i+1,gesture?"LOCK":"SEEK",c?c->name:"None",icon?objName(icon,iconName,sizeof(iconName)):"stock Sparkle_3",
              c?cgHitRadius(c):0.0f,nativeAimRange(),gesture?2:1);
    }
    if(gesture && (s.ready!=ready || s.radius!=radius)) {
        BYTE params[64]={0};
        *(DWORD *)(params+g_naB.readyParam)=ready?g_naB.readyMask:0;
        *(float *)(params+g_naB.radiusParam)=radius;
        g_ProcessEvent(g_aimFX[i],NULL,g_naB.ready,params,NULL);
        s.ready=ready;s.radius=radius;
    }
    // Keep the native effect where it belongs. Body clearance may suppress a
    // too-close free marker, but must not push a locked gesture off its target.
    float hx=at[0]-pl[0],hy=at[1]-pl[1],hz=at[2]-pl[2];
    s.visible=ready && placed && (gesture || !g_bodyClear || hx*hx+hy*hy+hz*hz>60.0f*60.0f);
    g_aimSup[i]=!s.visible;
    naVisible(i,s.visible); // setAimFXPaneVisible will isolate it before drawing
    return TRUE;
}
