# M5Stack Basic Breakout

A DX-Ball-flavoured Breakout for the M5Stack Basic Development Kit, driven entirely by the three front buttons. PlatformIO project, Arduino framework, M5Unified.

- **BtnA** left / menu up
- **BtnB** launch, pause, select
- **BtnC** right / menu down

---

## Build and flash

```bash
pio run -t upload
pio device monitor
```

Target is `m5stack-core-esp32`. The only dependency is `m5stack/M5Unified`, which pulls in M5GFX. Nothing else needs installing.

---

## Boot sequence

**1. Hardware init.** `M5.begin()` with an explicit config: display and speaker on, IMU and RTC off. Rotation set to landscape (320x240), brightness to a fixed default, screen cleared.

**2. Serial.** 115200 baud, used for build info and storage diagnostics only. Nothing in the game loop writes to serial.

**3. Storage probe**, in order, first success wins:

- `SD.begin(4)` on VSPI. A mounted card is then probe-written to confirm it is actually writable before being committed to. Backend = **SD**.
- Otherwise `LittleFS.begin(true)`, formatting on first mount failure. Backend = **FLASH**.
- Otherwise backend = **NONE**. The high score lives in RAM for this power cycle only, and a red dot appears at the right edge of the HUD so you know saving is off rather than silently broken.

**4. Load high score.** Reads `/breakout.dat` and validates magic, version and CRC16. Any failure yields 0 with no error shown to the player. A missing file is normal on first boot and does not trigger a write; the file is created the first time a score is actually saved.

**5. Settings init.** Difficulty defaults to Normal, sound to On. Session-only, not persisted.

**6. Audio init.** Speaker volume set low. A two-note startup blip plays if sound is on.

**7. Splash.** Title, loaded high score and detected storage backend for about 1.5 seconds. Any button skips it.

**8. Enter MENU.** The fixed-timestep loop begins. From here everything runs off `M5.update()` and the 60Hz accumulator.

---

## Controls

| State | BtnA | BtnB | BtnC |
|---|---|---|---|
| Splash | skip | skip | skip |
| Menu | cursor up | select / cycle | cursor down |
| Confirm reset | cursor up | confirm | cursor down |
| Playing | paddle left (held) | launch ball / pause | paddle right (held) |
| Paused | cursor up | select | cursor down |
| Powerup key | previous page | back | next page |
| Level clear | - | continue | - |
| Game over | - | back to menu | - |

Level clear and game over ignore BtnB while the grid is still shattering, so a mashed button cannot skip the animation it just triggered. Launching is also blocked during the level intro, since the ball would otherwise fire into rows that have not landed yet.

Menus use **edge detection** (one action per press). Paddle movement uses **level detection** (continuous while held). Mixing those two up is what makes button-driven paddles feel terrible.

Paddle speed ramps from 1.5 to 5.0 px/tick over about a quarter second of holding, so taps are precise and holds cover ground. This is the closest you get to DX-Ball 2's mouse feel with two direction buttons.

---

## Menu

```
  Start Game
  Difficulty    Normal
  Sound         On
  Powerup Key
  ---
  Reset High Score
```

Demo balls drift behind the menu while it sits idle, dimmed hard so they do not compete with the text for attention. They are drawn before the menu rows, so the text paints over them and they read as passing behind the words.

Difficulty and Sound cycle in place. **Reset High Score** opens a confirmation screen that always defaults to **No**:

```
  Erase high score 03500?

  > No
    Yes
```

The confirm step is deliberate. BtnB doubles as select and launch/pause, and a one-press wipe of the only persistent state in the game is exactly the thing you hit by accident. Choosing Yes writes 0, updates the HUD, and flashes "High score cleared" for a second.

If no storage backend came up, the item is greyed out and selecting it does nothing rather than pretending it worked.

Pause menu is Resume, Restart, Powerup Key, Quit to Menu. It draws as a panel so the frozen playfield stays visible behind it.

### Powerup key

Ten entries will not fit legibly on 320x240 with readable descriptions, so the key pages five at a time. BtnA and BtnC page, BtnB backs out. It is reachable from both the main menu and the pause menu, and the pause route is the one that matters: that is when something just landed on your paddle and you want to know what it did.

The swatches are drawn by `pill_draw()` in `pill.h`, the same routine the playfield uses. There is deliberately no second copy. A hand-written legend would look identical the day it was written and silently drift the first time a colour changed, which is the failure mode that makes a key screen worse than no key screen at all.

Adding a powerup is one edit: append it to the `DROPS` table in `entities.h` with a `name` and `desc`, and both the key screen and the catch label pick it up automatically.

---

## Difficulty

| | Ball speed | Paddle width | Lives |
|---|---|---|---|
| Easy | 2.0 px/tick | 60 px | 4 |
| Normal | 2.5 px/tick | 48 px | 3 |
| Hard | 3.2 px/tick | 36 px | 3 |

---

## HUD

20px bar across the top. Each field tracks its own dirty flag, so a score bump repaints about 50px instead of the whole bar, and nothing repaints at all on a frame where no value changed.

```
SCORE 01240        HI 03500        LV2   ***
```

Score is zero-padded to five digits so the field width never changes. The HI field shows the stored record, then tracks your live score once you pass it, so you can watch yourself set it. Lives render as small green blocks; a count of pips reads faster than a digit.

---

## Persistence

Binary record, not text:

```
magic    uint32   "BRKO"
version  uint16   1
score    uint32
crc      uint16   CRC16-CCITT over the preceding 10 bytes
```

Bad magic, wrong version or failed CRC all mean "treat as 0 and move on." A power cut mid-write on a plain text file would give you a plausible-looking wrong number instead; this way you just lose the record.

Written **only** on entry to Game Over, and only when the run beat the stored value. A typical session is one write, which makes flash wear irrelevant on a device like this.

**LittleFS, not SPIFFS.** SPIFFS is deprecated in the ESP32 Arduino core. LittleFS uses the same partition and the same API shape, with better crash resilience. Swapping back is a two-line change in `storage.cpp` if you need it.

---

## Gameplay

### Playfield

Play area runs from screen y=20 to y=239, drawn in canvas coordinates where 0 is screen row 20.

Bricks are a 10 x 8 grid, 28x10 each on a 32x14 pitch, stored as a flat `uint8_t bricks[80]` where the value is remaining hit points. The paddle sits at a fixed height and the ball is a 4x4 rect with float position and velocity.

### Serve

Each life starts with the ball stuck to the paddle centre. You can move freely while stuck. BtnB launches it, and the lean alternates left/right between serves so the opening shot is not perfectly predictable.

### Collision

Movement is **axis-separated**: move x, resolve, then move y, resolve. This handles corner hits correctly without needing the penetration-depth comparison a single-pass resolver requires.

- **Side and top walls:** flip the relevant component.
- **Bottom:** ball lost.
- **Paddle:** impact offset from centre sets the outgoing angle, up to 60 degrees off vertical. Speed magnitude is renormalised on every paddle hit so long rallies never drift faster or slower.
- **Bricks:** the ball's position converts directly to grid indices instead of looping all 80.

If `abs(vy)` drops below a floor value it gets nudged back up, so the ball cannot settle into a near-horizontal rally that never threatens a brick.

### Brick damage

Bricks show **the number of hits still required**, printed on the brick in white with a one-pixel dark shadow. Bricks needing one more hit show nothing at all: printing "1" across the large majority of the grid turns the playfield into a spreadsheet, and "no number" is a perfectly learnable way to say "one more hit".

This went through three versions. It started as a 1px white line at the top for 2HP and lines top and bottom for 3HP, which put about 3% of the brick's pixels in charge of the whole message. It then became `HP-1` notches cut through the body, so the brick read as chunks. Notches and a digit cannot coexist: a 6x8 glyph centred on a 26x10 brick lands exactly where the 2HP notch goes, and 2HP is the most common damaged state there is. The digit is the more precise of the two, so the notches went.

White-on-shadow rather than a shade derived from the brick colour, because the row palette spans yellow through dark blue and no single derived shade stays legible across all eight.

Every brick also gets a bevel, a lit top edge and a shadowed bottom edge derived from its own colour. Two lines, and it does more for how the playfield looks than anything else in the draw routine. Solid bricks carry corner rivets so they read as bolted down rather than as a brick that happens to be grey.

### Brick types

| | Behaviour |
|---|---|
| 1-3 HP | Damaged hits score nothing and add a white armour line. Only the killing hit scores. |
| Explosive | Clears its 8 neighbours. Adjacent explosives are cleared but do not chain, so a grid full of them cannot recurse into a stack overflow. |
| Indestructible | Never decrements. Level-clear counts only destructible bricks, or you could never finish. |

### Scoring

By row, top row worth most: 80, 70, 60, 50, 40, 30, 20, 10.

### Powerups

Destroying a brick has a 22% chance of dropping a capsule. Catch it with the paddle. Negative drops are mixed in with the good ones, which is what makes catching them a decision instead of a reflex.

**Positive**

| | Name | Effect | Colour |
|---|---|---|---|
| W | WIDEN | Paddle grows wider | green |
| M | MULTIBALL | Splits into extra balls | cyan |
| C | CATCH | Ball sticks, B releases | yellow |
| S | SLOW | Balls slow down, timed | light blue |
| + | LIFE | One extra life | magenta |
| X | DOUBLE | 2X score while lit | gold |
| G | BIG BALL | All balls go huge, timed | lime |
| E | EXPLOSIVE | Ball detonates bricks, timed | pink |
| L | LASER | Tap B to shoot, timed | violet |
| T | THROUGH | Ploughs 1-hit bricks, timed | hot pink |
| = | NET | One free missed ball | azure |

**Negative**

| | Name | Effect | Colour |
|---|---|---|---|
| N | NARROW | Paddle shrinks | red |
| F | FAST | Speeds up and ignites, timed | orange |
| V | DESCEND | Bricks drop one row | maroon |
| A | ARMOR | Every 1HP brick becomes 2HP | steel |
| H | HOLE | Gap opens in the paddle | dark red |
| K | BARRIER | Wall seals off the top | olive |
| Q | FOG | A band you cannot see into | slate |
| Y | REGROW | A few bricks come back | rust |

The table is **ordered, not just tagged**: everything before `PU_NEG_FIRST` is good, everything after is bad. The key screen pages off that boundary so a page never straddles the two groups, and `pickDropType()` uses it to scale negative frequency.

SLOW and FAST are both timed now. They used to step a persistent multiplier that nothing ever reset, so a single SLOW made the entire rest of the run sluggish and permanently extinguished the fire trail.

Five pills were cut to get here. SMALL BALL was a worse NARROW once BIG became timed. POINTS did nothing DOUBLE does not do better now that combos exist. FREEZE had the lowest impact in the table. The one-shot BOMB collapsed into the timed explosive ball. THROUGH was cut and later brought back with the 1HP rule that makes it interesting. Eighteen entries in two labelled sections reads far smaller than eighteen in one list, which is most of why the table could stay this size at all.

**Weights lean positive and scale with the level.** Negatives sit near a third of the table at level 1 and `pickDropType()` multiplies them by `1 + 0.08 × (level-1)`, capped at 2x. That reaches roughly 40% by level 5 and 46% by level 10, so the early game stays generous and the late game gets mean without any weight in the table changing.

**Shape carries valence, not just colour.** Good pills are rounded capsules; bad ones are square with their corners bitten out. The screen is small and often viewed in bright light, so you can dodge the right things without reading a letter.

Catching a pill floats its name above the paddle in its own colour. The key screen answers "what does E mean" when you have time to look; the catch label answers "what did I just catch", which is where most of the learning happens.

### Through ball

Passes through 1HP bricks, destroying them, and **bounces off anything with 2 or more hit points**, plus explosive and solid. So it ploughs weak rows but armour still stops it, which is what keeps it from being a free win: it rewards reading the grid rather than just holding the ball up there.

It burns while active, feeding the same fire trail, without touching the speed multiplier at all.

### Paddle hole

The HOLE gap is **drawn**, not just collided against. It originally existed only in `ballVsPaddle()`, so the ball passed through what looked like a solid paddle, which reads as a bug rather than as a powerup. The two paddle segments are derived from the same `HOLE_W` the collision test uses, so what you see is exactly what will catch.

### Barrier

A row of temporary indestructible blocks appears across the middle of the field, sealing off the bricks above it for six seconds.

Every other negative attacks the paddle, your vision, your reaction time or your progress. This one attacks the **space**, making part of the level briefly unreachable, which is a different problem to solve. It reuses solid-brick rendering and the rule that solids do not advance your combo.

It only fills empty cells so it never overwrites real bricks, skips any cell a ball is currently inside so nothing materialises on top of one, and remembers exactly what it placed so expiry removes that and nothing else.

### Laser

Manual fire on a **short BtnB press**, twin bolts from the paddle edges with a cooldown between volleys. Auto-fire would have dodged the control conflict, but tapping to shoot is what makes a paddle gun feel like a gun.

That puts three jobs on BtnB, resolved by priority: a held ball still launches first, a short press fires, and **pause moves to a long press** for the duration. Catching LASER pops a "TAP B" label on the paddle, because a control that silently changes meaning is worse than no control at all.

Bolts do not feed the rally combo. That counter measures what a ball has done since it last touched the paddle, and a bolt is not a ball.

### Fire trail

Heat is measured from a ball's **actual velocity** against the level's base speed, not from the speed multiplier.

Two reasons, and the second one was a bug. THROUGH lights the ball without touching speed at all, so a multiplier readout would miss it entirely. And SLOW used to push the multiplier below 1.0 permanently, with nothing ever resetting it, so once you caught a single SLOW the trail could not light again for the rest of the run. That is why the flame never appeared.

### Speed changes apply in flight

`setSpeedMul()` rescales the velocity of every ball already in the air, not just the multiplier.

The rescale is the whole point. `currentSpeed()` is only consulted when a ball bounces off the paddle, so setting the multiplier on its own did nothing until the next paddle contact. Catching SLOW felt delayed, and far worse, the timer expiring and restoring 1.0 did not speed the ball back up until it came down again. During a long rally up in the bricks that is many seconds, which is why SLOW looked like it never timed out even after the timer was added. Scale-ups are clamped so they can never push a ball past the tunnelling cap.

The trail ring buffer grew from 5 samples to 8. While burning, segments walk a white-yellow-orange-red-ember ramp by age instead of dimming one hue, each segment jitters a pixel sideways so it flickers rather than drawing a clean stripe, segment size tapers with age, and the ball puffs embers through the existing particle pool. Intensity scales with how far above base you are.

### Ball size

`BALL_SIZE` is no longer a constant. `g.ballSize` runs from 3 to 8 with a default of 4, and it is **global rather than per-ball**: catching BIG BALL with three balls in play grows all three, and a ball spawned by MULTIBALL afterwards inherits the current size because it reads the same field. BIG BALL is **timed** and jumps straight to the ceiling rather than stepping.

Getting there took three attempts. It started stepping one pixel per catch, which was invisible. Then it went to 6px, then 8px, each time by widening the brick gaps, because the resolver required the ball to fit between bricks. That approach was a dead end: it made the grid look like a picket fence for a ball that was still small.

The real fix was the collision resolver, not the geometry. See below. With that in place the ball goes to **12px, triple the default**, and brick width went back up to 28 with 4px gaps so the grid looks solid again.

Catching it now bursts particles from every ball and shakes the screen. The size change was always correct; what it lacked was a moment you could see happen. Its drop weight was also raised to 13 of 128, making it the most common positive, because at the old weight you could expect roughly half a BIG BALL per level and might genuinely never have caught one.

The bounds are geometry, not taste, and both are enforced by `static_assert`:

- **Maximum is no longer capped by the brick gap.** `resolveBricks()` used to stop at the first overlapping brick and push the ball flush against it, which parked a wide ball inside the neighbour and the two fought forever. It now gathers every overlapping brick, damages all of them, and resolves against the **deepest penetration** on the axis that moved. The gap stops mattering, and a wide ball smashing three bricks in one contact becomes the point rather than a bug to design around. A ball wider than the gap cannot be parked flush between two solid bricks because no such position exists, so in that one case it backs out along its own velocity instead of hunting for an edge.
- **Minimum is capped by the paddle.** At 2px the ball passes through the paddle between two frames at the speed cap. Three is the floor.

Growth expands each ball about its own centre, which can leave it overlapping a brick it was resting against by a pixel per side. That resolves on the next tick, pushing the ball flush and damaging the brick.

### Multi-ball

Up to 4 balls. You lose a life only when the **last** ball drains, not the first. Splitting a held ball gives the clones a fresh upward vector rather than cloning a zero velocity.

### Ball spin

The model is tangential **slip** at contact, not "paddle direction":

```
slip = ball.vx - paddleVelocity
```

That one number gives the whole behaviour without special-casing anything. A paddle chasing the ball at matching speed leaves near-zero slip and bounces clean. A paddle driven *into* the direction the ball came from leaves large slip, and bites hard. A stationary paddle leaves the ball's own vx, a mild natural amount.

Slip does two things: friction drags the ball toward the paddle's direction of travel immediately, and the same slip loads a persistent per-ball spin that curves the flight via a Magnus force perpendicular to velocity.

Spin carried **into** a paddle hit bends the departure angle, so it is something you set up on one bounce and cash in on the next rather than something that merely happens to you. Most of the charge is spent doing so, or a loaded ball would keep it forever. Position on the paddle still dominates; carry is a modifier.

Three things this gets right that are easy to get wrong:

- **Paddle velocity is measured, not intended.** It comes from the position delta after clamping, never from `dir * speed`. Holding left while already pinned against the wall means the paddle is not moving and must impart nothing; intended velocity would give phantom English off both walls, in exactly the situation the player is least able to explain.
- **Spin changes direction only.** Speed is renormalised immediately after the Magnus step. A spin that added energy would creep the ball past `BALL_SPEED_CAP` and start tunnelling through bricks between frames, quietly breaking a guarantee the whole collision system rests on.
- **The anti-stall floor gets the last word.** Spin is applied before it, and the outgoing paddle angle is capped at 70 degrees. Reversed or uncapped, a strong curve would drive `vy` toward zero and the floor would shove it back every tick, showing up as jitter rather than as a curve.

#### Three bugs that made v1 and v2 look like nothing

**Slip was measured from the outgoing velocity.** `ballVsPaddle` writes `b.vx` from the impact position, then slip was computed against that. So slip meant "where you hit, versus paddle velocity" rather than "ball going one way, paddle going the other", and a centre hit with a still paddle produced exactly zero slip by construction. The incoming vx is captured before anything overwrites it now.

**The tuning was done against a distance the ball never travels.** The simulations assumed a 200px traversal. The paddle sits at y=204 and the lowest brick bottom at y=132, so free flight before the first contact is about 72px, roughly 28 ticks. Every deviation figure from those rounds was about 3x optimistic, which is why nudging constants never helped: the basis was wrong, not the value.

**The kick and the curve were cancelling each other.** The worst of the three. Friction kicked the ball toward the paddle's direction of travel while the spin term, taking the opposite sign off the same slip, curved it back. The two spent the whole flight fighting, so the path came out nearly straight no matter how hard the constants were pushed. Both terms take the same sign now and reinforce.

Deflection over the real 72px hop, ball arriving at vx +1.8:

| paddle | kick | curve on top | total |
|---|---|---|---|
| still | 10px | 5px | 15px |
| 2 px/tick into the ball | 21px | 9px | 30px |
| 5 px/tick into the ball | 38px | 13px | 51px |
| 5 px/tick with the ball | 18px | 8px | 26px |

#### Tuning

Everything lives in one block in `config.h` marked THE TUNING BLOCK.

| Constant | v1 | v2 | v3 | What it does |
|---|---|---|---|---|
| `SPIN_MAGNUS` | 0.018 | 0.030 | **0.090** | How bent the flight looks |
| `SPIN_FRICTION` | 0.12 | 0.12 | **0.20** | Immediate sideways throw off a sweeping paddle |
| `SPIN_BOUNCE_RETAIN` | -0.45 | -0.70 | -0.70 | Spin kept and flipped on wall/brick contact |
| `SPIN_DECAY` | 0.985 | 0.990 | 0.990 | How long a curve lasts |
| `SPIN_GAIN` | 0.12 | 0.14 | 0.14 | Slip into stored spin |
| `SPIN_CARRY` | - | 0.30 | 0.30 | Radians of departure bend per unit incoming spin |
| `SPIN_CARRY_CONSUME` | - | 0.30 | 0.30 | Spin left after being cashed in |
| `SPIN_WAKE_PX` | - | 3.2 | 3.2 | Lateral sway of the trail at full spin |

Two rounds of tuning failed before the actual causes were found, and neither was the value of a constant.

**Slip was read from the wrong velocity.** `ballVsPaddle()` computed the outgoing `vx` from the impact position, then measured slip against *that* rather than against the ball's incoming motion. Since a centre hit produces an outgoing `vx` of exactly zero, a centre hit with a stationary paddle produced exactly zero slip by construction, removing the natural spin a still paddle is meant to impart. The incoming velocity is now captured at the top of the function before anything overwrites it.

**The tuning was measured against a distance the ball never travels.** Both earlier rounds modelled a 200px traversal. The paddle sits at y=204 and the lowest brick bottom is at y=132, so the free flight before the ball strikes something is **72px**, about 28 ticks. Every deviation figure quoted in v1 and v2 was roughly 3x optimistic, which is exactly why raising the constant twice changed nothing perceptible.

Measured over the real 72px hop, centre hit, ball arriving at vx +1.8:

| Paddle | slip | spin | instant angle | curve |
|---|---|---|---|---|
| still | 1.80 | 0.25 | 8° | 8px |
| sweeping 2.5 px/tick | 4.30 | 0.60 | 20° | 20px |
| sweeping 5.0 px/tick | 6.80 | 0.95 | 33° | 30px |
| moving with the ball | -3.20 | -0.45 | 15° | 15px |

For comparison, v2 on a centre hit with a still paddle produced 0° and 0px, and with a full sweep produced 14° and under 8px of curve.

The risk case is a long clean run late in a level once the bottom rows are gone, where the ball does get its 200px and will bend hard. `SPIN_DECAY` is the knob for that.

#### Measuring rather than guessing

`#define SPIN_DEBUG 1` at the top of the tuning block prints one line per paddle hit:

```
[spin] inVx +1.80 padVel -5.00 slip +6.80 -> spin +0.95 | outVx -1.36 ang -33.0deg | curve 1.96 deg/tick
```

That separates "is spin being generated" from "is it visible once it is", which is the distinction two rounds of blind tuning could not make.

#### Seeing it

The trail sweeps sideways in proportion to spin and age, so the wake streams off one side of the arc. The sampled positions already contain the true curve, but on a 4px ball that curve is too small to read, which is the other half of why v1 seemed to do nothing.

A single pixel also orbits the ball at a rate set by the spin, showing which way it is loaded before it curves. That one is only drawn at 6px and up, so it appears with BIG BALL and stays off at the default size where it would be noise.

Spin zeroes on serve and on a sticky-paddle catch.

### Rally combo

Every brick a ball touches without returning to the paddle raises that ball's chain, and each brick in the chain scores 10% more than base. Non-fatal hits on armoured bricks count too: the chain measures work done on the grid, not kills.

The counter is **per ball, not global**. With three balls up, each keeps its own run, and one ball returning to the paddle does not wipe the streak the other two are building. A ball spawned by MULTIBALL inherits its parent's chain, which is the consistent reading of "bricks hit since the last paddle touch".

| Chain | Multiplier | Top-row brick |
|---|---|---|
| 1 | 1.0x | 80 |
| 5 | 1.4x | 112 |
| 10 | 1.9x | 152 |
| 11+ | 2.0x (cap) | 160 |

A twelve-brick rally pays 1480 instead of a flat 960, about 54% more. Stacked with DOUBLE the ceiling is 4x base on a single brick.

Three rules keep it honest:

- **Solid bricks do not advance the chain.** They are an infinite source of contacts, so a ball wedged between two of them would farm the multiplier to its cap and hold it there forever. This is the exploit the mechanic invites, and it is closed explicitly.
- **Blast victims are paid at the current chain but do not advance it.** Otherwise one bomb jumps the multiplier most of the way to the cap, rewarding the pickup instead of the rally.
- **The cap exists because the ceiling is otherwise unbounded.** THROUGH lets a single pass plough an entire row without a paddle touch.

Only the paddle resets the chain. Walls do not, and neither does the safety net, since the net is a rescue rather than a rally.

Feedback is continuous rather than only at payout: score popups show the chain from 2 up, both the popup and the ball's trail brighten as the run grows, a chain of 4 or more announces itself at the paddle when it banks, and the longest rally of the game is shown on the game over screen.

### Descending grid

One DESCEND pill lowers the entire grid by exactly one row. It is a single step, not a repeating sequence: the pill is the event. The drop eases in over 45 ticks, about three quarters of a second, so you watch it arrive rather than finding it already there.

It is implemented as a pixel offset behind one accessor, `gridTopY()`, used by both the drawing and the collision index maths. Shuffling the array would mean two places that have to agree about where the bricks are, and a ball bouncing off bricks that are not where they are drawn is an unfindable bug.

**Crush.** When the lowest occupied row reaches `PADDLE_Y - 4`, the run ends outright. Remaining lives are forfeit, which is the point of a loss condition you can watch approach for 45 ticks and still fail to clear. Solid bricks count toward it; being crushed by something indestructible is exactly as fatal.

How many drops a level survives depends on how far down its layout reaches, which is the right relationship. A layout that fills the grid is inherently closer to the floor:

| Layout reaches | Drops survivable |
|---|---|
| Row 3 (L01) | 8 |
| Row 5 (L02) | 6 |
| Row 6 (L05, L06) | 5 |
| Row 7 (the rest) | 4 |

The death line stays hidden until the grid has actually started descending. An always-on line reads as scenery, and a loss condition you have stopped noticing is not a warning.

Empty rows in a layout belong at the bottom, never the top. Headroom above the bricks is the space DESCEND eats, so a layout that gives its top rows away starts the level already partway to the floor. L01 and L02 originally did exactly that, which also left the grid floating 40px below the HUD on the first screen of the game.

### Progression

Clearing every destructible brick advances a level, which multiplies ball speed by 1.06 (capped) and loads the next of 10 hand-built layouts stored in PROGMEM. Past level 10 the table wraps and the upper half of the grid gains a hit point each lap. Paddle width and speed multiplier carry over; temporary effects do not.

Losing a ball costs a life, clears any falling drops, and re-serves after a short pause during which you can still reposition the paddle.

---

## Effects

Every effect below draws into the canvas and costs only the drawing itself. There is no erase bookkeeping and no smearing when two of them overlap, which is what made most of these impractical under the dirty-rect design the project started with.

| Effect | What it does |
|---|---|
| Score popups | Brick value floats up from where it died and fades. Also used for powerup names and the +250 bonus. |
| Brick shatter | 5 particles per brick, coloured to the brick; explosive bricks throw 16. |
| Screen shake | Explosive bricks, lost balls, bad pills, game over. Decays with its remaining time rather than stopping dead. |
| Ball trail | Last 5 positions, dimming. A playability win as much as a looks one: at the speed cap a 4px ball is a strobing dot. |
| Paddle impact | Flashes white and grows a pixel for 4 ticks on contact. |
| Brick damage flash | Two-frame white overlay so a non-fatal hit on an armoured brick is unmistakable. |
| Rolling score | The HUD counts up to the new total instead of jumping. |
| Chain heat | Popup and ball trail brighten as the rally combo builds. |
| Level intro | Rows drop in from above, staggered top to bottom, with the level number over them. Physics is held until they land. |
| Level clear / game over | The remaining grid shatters a few bricks per tick before the panel appears. |
| Pill wobble | Purely cosmetic sine offset applied at draw time; `d.x` never moves, so the hitbox is always where it looks. |
| Menu attract mode | Demo balls behind the main menu. |
| Safety net | Dashed line below the paddle, consumed on the save, with a SAVED popup. |
| Cursor pulse | Breathing highlight on the selected menu row. |

Floating text is drawn transparent with a one-pixel drop shadow. LovyanGFX decides transparency by comparing the two text colours: single-argument `setTextColor(col)` sets fore and back to the same value, which makes `fillbg` false in the glyph blitter and leaves the background untouched. The two-argument form was what painted a black box behind every popup. The shadow costs one extra draw and keeps the text readable over particles and bricks. Menu rows stay opaque on purpose, since those rely on glyph boxes painting over the attract balls.

Fading on an 8-bit palette canvas has no cheap alpha blend, so a fade is a walk down a ramp of darker colours via `dim565()`. At 256 palette entries it bands slightly on the darkest steps, which at this pixel size reads as a fade rather than an artifact.

The grid teardown runs 3 bricks per tick rather than all at once. Shattering 80 bricks in one frame would ask for ~240 particles from a 64-slot pool: the first dozen bricks would drain it and the rest would emit nothing, so the effect would visibly bias to the top-left. Spreading it over the outro lets the pool recycle, and the sweep reads as a wave.

The rolling score is the one animation here with a real per-frame cost, because the HUD is not on the canvas: it repaints its field on every frame it is moving. Bounded and fine, but it is not free the way the canvas effects are.

---

## Frame budget

The build prints a timing line to serial every 3 seconds:

```
[perf] 34.2 fps | render 27.10 ms | push 25.80 ms | draw 1.30 ms | heap 118304
```

`push` is the fixed SPI toll for shipping the canvas to the panel. `draw` is what the animation work actually costs. **Check this before adding more effects.** If `push` dominates, as it does on a stock 40MHz bus, then particles and trails are effectively free: you are already paying the fixed cost every frame and the drawing is noise beside it. If `draw` starts climbing toward `push`, the pools need budgeting.

A full 320x220 canvas at 8bpp converts to 16-bit on the way out, which is roughly 1.1 Mbit per frame and puts the ceiling somewhere near 30-35 fps rather than 60. The fixed timestep means the game still runs at the correct speed; the accumulator just does two logic ticks per rendered frame.

---

## Rendering

The play area is an **8-bit canvas** (`M5Canvas`, 320x220, about 70KB) pushed in one `pushSprite` per frame. The HUD stays on the direct framebuffer with per-field dirty tracking.

The original plan called for dirty-rectangle redraw of the ball and paddle, which is cheaper. That falls apart the moment you have four balls and a dozen falling capsules on screen, so the canvas was worth the memory. Expect roughly 30-40 fps of rendering against a 60Hz physics tick; the fixed-timestep accumulator decouples the two, so a slow frame changes how smooth it looks, never how fast it plays.

If `createSprite` fails at boot, the game reports it on screen and over serial rather than silently drawing nothing.

---

## Layout

```
platformio.ini
src/
  main.cpp       boot sequence, fixed-timestep loop, frame timing
  config.h       all geometry, tuning, colours, dim565(), invariants
  entities.h     game state enum, ball/paddle/drop structs, DROPS table
  gfx.h/.cpp     owns the shared canvas; push timing and the [perf] line
  fx.h/.cpp      particles, floating popups, screen shake
  pill.h         the one and only powerup pill renderer (template)
  game.h/.cpp    state machine, physics, collision, rendering
  levels.h/.cpp  10 PROGMEM layouts
  menu.h/.cpp    main menu, powerup key, pause menu, attract mode, screens
  hud.h/.cpp     top status bar with per-field dirty tracking, rolling score
  input.h/.cpp   edge vs level button state
  audio.h/.cpp   non-blocking blips
  storage.h/.cpp SD, then LittleFS, then nothing
```

---

## Notes and gotchas

**`bricksLeft` is derived, not maintained.** It is recomputed once per tick in `tickPlaying()`. It used to be updated incrementally at each site that mutated the grid, and the BOMB path took an early `return` past the single line that did it. A bomb clearing the last destructible bricks therefore left the count stale and non-zero, and since no bricks remained to damage, nothing ever refreshed it. The level could not clear, ever. The `bool` that `damageBrick()` returned was never read by its one caller, which is what made the early return look harmless. It returns `void` now.

The general lesson: an 80-byte scan per tick is nothing beside the canvas push, and derived state that is cheap to recompute should be recomputed rather than maintained.


**The geometry rules in `config.h` are load-bearing.** Two `static_assert`s enforce them, and both exist because the alternative is a bug you only find after several minutes of play:

- The gap between bricks must be at least `BALL_SIZE`. With a smaller gap, pushing the ball clear of one brick leaves it embedded in the brick stacked behind it, and it jitters between the two forever. This is why bricks are 28x10 on a 32x14 pitch rather than the more obvious 30x12.
- `BALL_SPEED_CAP` must stay under `BRICK_H`, or the ball tunnels through a brick between two frames.

**Float rects, not pixel rects.** The brick cell-range scan covers `x` to `x + BALL_SIZE`, not `x + BALL_SIZE - 1`. The `-1` is integer-pixel thinking and silently drops sub-pixel overlaps, which is how a ball ends up embedded in a brick it was never tested against.

**`M5.update()` must run every loop** or no button ever registers.

**No `delay()` in the game loop.** It fights the timestep accumulator.

**SD shares VSPI with the display.** This works in practice on the Basic, but if you see display corruption after inserting a card, drop the SD clock in `storage.cpp` from 25MHz.

**Multi-note sound effects queue.** `M5.Speaker.tone()` stops the current sound by default, so a two-note effect would otherwise only ever play its second note.

---

## Not included

Things considered and deliberately left out:

- **Chain-reacting explosive bricks.** A full grid of them would recurse into a stack overflow, so an explosion clears its explosive neighbours without detonating them.
- **Laser / paddle gun.** Every button is already assigned, and overloading BtnB with fire means dropping pause during play.
- **Background music.** Single-channel speaker sharing a timer with the effects; it would stomp them or need mixing.
- **Persisted settings.** Only the high score is saved. The record has a version field, so adding them later is a clean migration.
