### 1. Momentum Chamber (portal falling puzzle)

Build a vertical shaft with two portal surfaces.

Goal:

Player must fall through one portal and come out the other with enough speed to reach a high ledge.

What breaks if math is wrong:

speed disappears
exit direction is wrong
player can’t reach target

👉 Forces:

velocity transform
direction consistency
### 2. Sideways Exit Puzzle

Place two portals:

one on the floor
one on a wall

Goal:

player jumps in and must come out sideways to reach a platform

If wrong:

player exits facing wrong direction
camera snaps incorrectly

👉 Forces:

look direction transform
orientation consistency
### 3. Portal Alignment Puzzle

Create tight corridors where:

portals must be placed precisely to work

Goal:

incorrect placement makes traversal impossible

👉 Forces:

correct portal positioning
understanding of surface normals
### 4. “No Fit” Portal Puzzle

Design walls with edges and gaps.

Goal:

player must find surfaces where portals actually fit

👉 Forces:

reasoning about surface bounds
spatial awareness (not just “shoot anywhere”)
### 5. Sliding Ramp Puzzle

Add a sloped ramp leading into a portal.

Goal:

player must slide down and use gained direction/speed correctly

If wrong:

player sticks or behaves unnaturally

👉 Forces:

velocity projection
surface interaction
### 6. Moving Platform + Portal Puzzle

Have a moving platform and a portal destination.

Goal:

time movement so player exits portal onto moving platform

If wrong:

player desyncs or falls through

👉 Forces:

relative motion
timing + position prediction
### 7. Rotating Platform Puzzle

Place a rotating surface where portals can be placed.

Goal:

player must use rotation to redirect themselves

If wrong:

exits feel inconsistent
direction math breaks

👉 Forces:

changing coordinate systems over time
### 8. Cube Weight Puzzle

Add a button that requires a crate.

Goal:

player must transport cube using portals

👉 Forces:

applying portal logic to objects (not just player)
### 9. Throw Through Portal Puzzle

Require player to throw a cube:

into one portal
out another to hit a target

If wrong:

trajectory breaks
cube exits wrong direction

👉 Forces:

velocity transform on objects
### 10. Laser Reflection Puzzle

Laser bounces off surfaces to hit a target.

Goal:

place portals or mirrors to guide beam

If wrong:

reflections look wrong immediately

👉 Forces:

reflection math (dot product intuition)
### 11. Portal Laser Puzzle

Laser must go through portals to reach goal.

👉 Forces:

combining two systems:
reflection
portal transforms

This is where understanding really locks in.

### 12. Multi-Portal Momentum Chain

Design a sequence:

fall → portal → sideways → portal → upward → target

Goal:

build speed across multiple transitions

If wrong:

errors accumulate fast

👉 Forces:

consistency across repeated transforms
### 13. Timing Puzzle (Portal + Movement)

Combine:

moving platform
portal placement
jump timing

Goal:

precise execution required

👉 Forces:

understanding motion over time (not just static math)
### 14. “Impossible Room” Puzzle

Design a room where:

geometry seems disconnected
portals make it navigable

👉 Forces:

thinking in relative spaces, not world space
### 15. Chase Object Through Portals

Have an object moving continuously.

Goal:

player must place portals to redirect it to a goal

👉 Forces:

prediction of motion
dynamic use of transforms