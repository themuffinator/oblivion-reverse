# Monster AI Discrepancies

## monster_cyborg

### HLIL initialization snapshot (0x10025234–0x100253da)
- Sound table loads a full set of cyborg effects: three melee shots, death, idle loop, two pain variants, sight/search calls, three footfalls, and a heavy landing thud.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27887-L27899】
- Spawns the stock model with a bounding box of {-16, -16, -38} to {16, 16, 27}, 300 HP, and -120 gib health by writing the model path, mins/maxs, `health`, and `gib_health` fields directly.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27902-L27908】
- Core AI helpers are wired to specific routines: `sub_100245d0` (stand) → `data_10051540` (frames 0x7d–0x7d), `sub_100245e0` (idle loop) → `data_10051560` (frames 0x7d–0x7d), `sub_10024610`/`sub_10024620` (locomotion state machine using additional mmoves such as `data_100516a0` at frames 0x12–0x17 and `data_10051730` at frames 0x4f–0x51), and a randomised attack dispatcher `sub_10024e00` that selects between mmoves `data_10051958` (0x18–0x23), `data_10051a48` (0x2f–0x34), and `data_10051aa0` (0x35–0x3a).【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27909-L27918】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L68309-L68428】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L68428-L68434】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L68659-L68770】
- Pain handling `sub_100246a0` throttles reactions through the `*(self+0x210)` timer, randomly alternates between `mutpain1` and `mutpain2`, and swaps to mmoves `data_100516f8` (0x49–0x4e) or `data_10051730` (0x4f–0x51).【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27892-L27894】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27429-L27467】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L68457-L68480】
- Death and gib logic routes through `sub_100250b0`, which plays `mutdeth1`, toggles corpse flags, and triggers complex gibbing when `health` falls below thresholds.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27915-L27920】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27660-L27727】

### Current source implementation (`src/game/m_cyborg.c`)
- Defines compact frame spans (stand 0, walk 1–4, run 5–8, attack 9–12, pain 13–14, death 15–17) with short mframe arrays.【F:src/game/m_cyborg.c†L13-L191】
- Replaces the multi-sample weapon audio with a single `deatom/dfire.wav` shot and only keeps `mutpain1` as the pain cue; no dedicated thud channel is played.【F:src/game/m_cyborg.c†L209-L217】【F:src/game/m_cyborg.c†L149-L163】
- Attack behaviour fires every 1.2 s via `monsterinfo.attack_finished` and reuses a two-step `cyborg_attack_think` callback inside four zero-distance frames.【F:src/game/m_cyborg.c†L120-L147】

### Discrepancies
- **Movement coverage** – Retail HLIL drives distinct stand, idle, walk/run, and attack mmoves across frame IDs 0x12–0x7d, while the reconstruction collapses everything into 0–17, losing the original pacing, footstep timing, and transitions.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27909-L27923】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L68309-L68434】【F:src/game/m_cyborg.c†L13-L145】
- **Attack cadence & variety** – `sub_10024e00` randomises among three ranged sequences and seeds per-frame callbacks, whereas the C version runs a fixed four-frame burst with a single cooldown and no variation.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27660-L27684】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L68659-L68770】【F:src/game/m_cyborg.c†L120-L147】
- **Pain handling** – HLIL alternates between two pain sounds, enforces a three-second debounce through `*(self+0x210)`, and can branch to separate mmoves; the port plays one sound and hardcodes a two-second pause with a two-frame flinch.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27429-L27467】【F:src/game/m_cyborg.c†L149-L163】
- **Audio fidelity** – Original code keeps the three `mutatck` shots and a landing `thud1`; the new code swaps in `deatom/dfire` and never emits the heavy impact sound, altering player feedback.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27887-L27899】【F:src/game/m_cyborg.c†L209-L217】
- **Behavioural flags** – HLIL toggles `*(self+0x3c)` to force stand-ground behaviour when health drops and uses scripted callbacks `sub_10024920` on timers, none of which are mirrored in the simplified translation.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L27435-L27539】【F:src/game/m_cyborg.c†L120-L147】

## monster_spider

### HLIL initialization snapshot (0x1002ddb5–0x1002df3b)
- Reuses gladiator melee, idle, search, and dual pain sounds, plus mutant thud effects, but overrides the sight cue with `spider/sight.wav`. All clips are loaded alongside the model and bounding box before spawning.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L34169-L34191】
- Establishes 0x190 (400) HP with the same gib health and spawns a 64×64×64 hull, matching the heavier tank profile.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L34180-L34191】
- Helper wiring mirrors the retail DLL: `sub_1002d440` (stand) → mmove `data_1005ca00` (frames 0x00–0x36), `sub_1002d450` (walk/run staging) → `data_1005ca88` (0x37–0x40), attack controller `sub_1002d660` picks between `data_1005cb10` (0x41–0x4a) and `data_1005cb68` (0x4b–0x50), while melee combos escalate into `data_1005cbb8` (0x63–0x67).【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L34206-L34219】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L33792-L33925】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L82679-L82839】
- Pain and recovery logic runs through `sub_1002d570`/`sub_1002d610`, which manipulate the `*(self+0x35c)` flags and a timer at `*(self+0x1fc)` before clearing into the idle loops; death funnels through `sub_1002dbd0` with extended gibbing when required.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L33841-L33870】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L33940-L34200】

### Current source implementation (`src/game/m_spider.c`)
- Uses condensed frame definitions (stand 0, walk 1–6, run 7–12, attack 13–18, pain 19–21, death 22–28) with repeating walk/run cycle patterns and a three-hit melee combo triggered by fixed attack frames.【F:src/game/m_spider.c†L10-L172】
- Sound setup keeps gladiator melee, pain, idle, and search clips plus the spider sight cue, but only plays a single thud for both steps and death without exposing the secondary pain voice from the HLIL build.【F:src/game/m_spider.c†L189-L207】

### Discrepancies
- **Animation breadth** – Retail AI walks and runs across 0x00–0x50 frame ranges with multiple attack chains up to 0x67, while the reimplementation compresses everything into 0–28 frames, flattening the elaborate melee choreography.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L33792-L33925】【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L82679-L82839】【F:src/game/m_spider.c†L10-L121】
- **Attack cadence** – `sub_1002d660` and `sub_1002d750` randomise between two melee chains and gate follow-ups via the `*(self+0x35c)` flag and timers, whereas the C port fires three deterministic claws over six static frames with a flat one-second cooldown.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L33874-L33925】【F:src/game/m_spider.c†L93-L122】
- **Pain / recovery behaviour** – The DLL tracks stagger state in `*(self+0x268)` and resumes combat via `sub_1002d570` and `sub_1002d610`, but the rewrite simply plays a three-frame flinch without state bookkeeping.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L33841-L33870】【F:src/game/m_spider.c†L124-L139】
- **Audio cues** – Original code alternates between `gladiator/pain.wav` and `gladiator/gldpain2.wav` and uses `mutant/thud1.wav` separately for death and impacts; the simplified version never references the secondary pain clip and reuses the thud for footsteps only.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L34169-L34191】【F:src/game/m_spider.c†L189-L207】
- **Spawnflag handling** – HLIL checks spawnflag bit 8 to swap to a boss-sized bounding box and 7-frame idle set, including forced MOVETYPE adjustments; no equivalent branching exists in the port so all variants share one physics setup.【F:references/HLIL/oblivion/gamex86.dll_hlil.txt†L34192-L34205】【F:src/game/m_spider.c†L182-L207】

## Retail footage status
Surviving DM2 recordings (`demo1.dm2`, `demo2.dm2`) are present under `pack/demos/`, but the current toolchain lacks a Quake II demo player, so no fresh footage could be captured or reviewed to corroborate ambiguous HLIL states.【40f0a8†L1-L2】
