# Touch calibration

Read this only when changing calibration. The maintained API is [touch_calibration_core.h](../../firmware/pocket_agent/touch_calibration_core.h); guided collection is [touch_calibration_session.h](../../firmware/pocket_agent/touch_calibration_session.h). [Portable tests](../../tests/touch/test_contract.cpp) define constants and binary compatibility.

## Required behavior

`kCardinalTargets` must contain `(234,63)`, `(234,403)`, `(64,233)`, and `(404,233)` in that order: up, down, left, right. Each is 170 pixels from `kCenter` at `(234,233)`.

The visible interaction is modeled after a controller-stick calibrator. Show a white center dot and one orange destination dot with no connecting line. For each cardinal step, use the exact sentence `Drag from the center {DIRECTION} to the orange dot`, replacing the token with UP, DOWN, LEFT, or RIGHT. The user touches the center, slides to the orange dot, and holds; a captured destination turns green. Firmware must not accept the stationary center as the endpoint. It begins the stable endpoint window only after raw movement of at least 80 units from the initial contact. Releasing early or returning toward center repeats that direction.

`reduceSamples` accepts only finite points with at least 16 samples collected over at least 80 ms.
It rejects a set when either axis has a max-minus-min span greater than 12.
For an even number of samples, the median is the mean of the two middle sorted values on each axis.

`reduceAttempt` accepts a touch release containing at least one finite point, ignores non-finite points when a finite point is also present, and returns the component-wise median of the finite points. It has no duration, span, or target-distance threshold because a quick miss is valid calibration data.

`asteroidTarget` returns false for a null output or an attempt outside `[0, 51)`. For a valid attempt, compute `id = (attempt * 20 + 7) % 51`. Ids 0–2 are center targets with `radialLayer = 0` and `sector = 255`; ids 3–18 use radius 70 with `radialLayer = 1` and sector `id - 3`; ids 19–34 use radius 120 with `radialLayer = 2` and sector `id - 19`; ids 35–50 use radius 170 with `radialLayer = 3` and sector `id - 35`. Sector 0 is up and sectors increase clockwise. Set shape from `attempt % 4`, visual radius from `{10,14,18,22}[(attempt * 3 + 1) % 4]`, and RGB color from `{0xff7a00,0x00d9ff,0x9dff00,0xff3bd4,0xffffff}[(attempt * 2 + 3) % 5]`. This deterministic permutation covers every required location exactly once while varying the target art independently of calibration truth.

`fitAffine` uses all supplied point pairs in an overdetermined least-squares fit.
It accepts four or more nondegenerate pairs, returns finite coefficients, and rejects singular input.
It must handle scale, offset, swapped axes, inverted axes, skew, and small measurement noise.

`validAffine` requires six finite coefficients and an absolute linear determinant `abs(a*e - b*d)` of at least `1e-9`.

`applyWarp` first applies the affine transform. It then interpolates a residual vector in polar display space: angular interpolation wraps between sector 15 and sector 0; radial interpolation runs from the center residual through rings at 70, 120, and 170 pixels; points outside 170 pixels use the outer-ring value. Add the interpolated residual to the affine point. This is a local scale-and-shape correction rather than a single pixel shift.

`zeroWarp` creates a map with the supplied affine transform and zero residuals, so its output exactly matches `apply`.

`validWarp` requires a valid affine transform, finite residuals, residual magnitude no greater than 48 pixels, and Euclidean difference no greater than 32 pixels between angular neighbors, radial neighbors, and the center and first ring. Angular neighbor validation wraps across sector 15/0. These bounds limit abrupt local warping caused by a noisy tap.

`smoothRing` applies a circular `0.25 previous + 0.5 current + 0.25 next` kernel independently to x and y. Sector 0 uses sector 15 as its previous neighbor. Build the stored inner, middle, and outer residual rings through this smoothing step so one imperfect tap cannot create a sharp local jump.

`averageRings` combines the clockwise and counterclockwise outer-ring measurements with equal weight in every sector, independent of how many raw samples either pass produced. It must permit either input array to alias the output. Keeping the two passes separate until this step exposes direction-dependent drag or controller hysteresis instead of allowing the longer pass to dominate the map.

## Guided data construction

The complete exercise has three ordered stages: four cardinal drags, two Around the World passes, and Asteroid Shooter. A new map is provisional until every required measurement exists and `validWarp` succeeds. Canceling at any stage discards the provisional data and leaves the active map and saved record unchanged.

The cardinal stage supplies the four raw/screen pairs for the affine seed. Its visible rule is strict: one white center dot, one orange destination dot, and no connecting line, ray, arrow shaft, or orange trail. The instruction is exactly `Drag from the center UP to the orange dot`, with DOWN, LEFT, and RIGHT substituted for the remaining stages.

Around the World collects two distinct 16-sector datasets in this order: clockwise, then counterclockwise. Each dataset requires at least four finite samples per sector, survives lifts, permits backtracking and extra rotations, and never rejects a sample for radial distance. For each sample, apply the affine seed, project the result to radius 170 without changing its observed angle, and accumulate `projected - affineMapped` as the residual. Reduce each pass to one residual per sector. Report the Euclidean difference between the two pass residuals per sector, combine them with `averageRings`, and pass the combined result through `smoothRing` to obtain the outer ring. Sample count cannot give one direction more weight than the other.

Asteroid Shooter begins with a separate black introduction screen containing only `ASTEROID SHOOTER` and `Tap to start`. The tap that starts the stage is discarded after release and never becomes calibration data.

The active test uses 51 target attempts in a deterministic order: three at the center, 16 at radius 70, 16 at radius 120, and 16 at radius 170. The three rings place targets on exact 22.5-degree sector nodes. The 170-pixel ring deliberately exercises the lower and outer display. Styles cycle across filled circles, outlined rings, hexagonal rocks, and irregular asteroids; visual radii cycle across 10, 14, 18, and 22 pixels; colors cycle across orange, cyan, lime, magenta, and white. Every style contains a 2-pixel white center pip. Style, size, and color never change the mathematical target center.

During those 51 attempts, draw a black background and the current target only. Do not draw a title, directions, attempt count, progress, footer, button hint, previous target, impact marker, miss vector, toast, or result. This rule leaves every display region available for measurement, especially the lower face. Advance directly to the next target after a touch release.

Every touch release containing at least one finite raw sample is an attempt, including a visible miss. Use a component-wise median when the contact produced more than one finite sample. Record target id, target center, raw median, affine-mapped point, residual `target - affineMapped`, and Euclidean miss distance. Never reject an attempt because the mapped touch is outside the visible target. BtnA/left may undo exactly the latest accepted attempt before collection proceeds.

Use the component-wise median of the three center residuals for `WarpMap::center`. Put each target residual into its matching angular node, then call `smoothRing` on all three completed rings. Combine the smoothed 170-pixel shooter ring with the smoothed, equal-direction-weight perimeter ring using `averageRings`; smooth that combined outer ring once more. Validate the whole map. If validation fails, identify and recollect only the measurements adjacent to the unsafe residual; do not restart completed regions.

`verify` measures Euclidean distance for each pair.
It passes when the mean error is at most 12 pixels and the maximum error is at most 22 pixels.
Zero points is a failure.

`perimeterSector` divides the round face into 16 equal angular sectors. Sector 0 is straight up and sector numbers increase clockwise. It returns `kPerimeterSectorCount` for a non-finite point.

`analyzePerimeter` is a collection-progress function, not an accuracy test. It counts finite observations in each sector and reports complete after every sector contains at least four samples. It accepts clockwise, counterclockwise, backtracking, multiple rotations, and lifting and resuming. It does not reject radial distance from the guide ring: those samples are the data used to calibrate the perimeter.

`makeRecord` fills the fixed metadata, casts the six coefficients to float in contract order, sets `reserved` to zero, and calculates the checksum.
`validateRecord` checks every metadata field, the exact 40-byte record size, finite coefficients, the determinant threshold, and checksum equality.

The checksum is CRC-32/ISO-HDLC: reflected polynomial `0xedb88320`, initial value `0xffffffff`, and final XOR `0xffffffff`.
Feed these fields in order, least-significant byte first:

1. `magic` as 4 bytes
2. `version` as 2 bytes
3. `width` as 2 bytes
4. `height` as 2 bytes
5. `rotation` as 1 byte
6. `reserved` as 1 byte
7. each coefficient's IEEE-754 float bit pattern as 4 bytes, in array order

Do not include `checksum` itself.

`RecordV2` uses the same CRC-32 algorithm. Feed every field before `checksum` in declaration order, least-significant byte first, including generation, the six affine floats, two center floats, and all residual floats in radial-major, sector-major, x/y order. `makeRecordV2` writes version 2 and the fixed geometry counts. `validateRecordV2` checks metadata, exact 440-byte size, checksum, generation-independent map validity, and all fixed dimensions. `warpFromV1` accepts only a valid version-1 record and returns its affine transform with zero residuals.

## Firmware integration

Configure the panel before loading `espt-touch/record`. Use raw points through the complete warp for every application touch. Keep collection aggregates off the loop stack; emit phase transitions, progress and stack headroom without changing collection behavior. Reject invalid storage and open calibration; a valid version-1 record remains usable with zero residuals and can be explicitly upgraded.

Save a 440-byte version-2 record only after the complete map validates, increment generation, and apply it immediately. Cancel preserves the prior map. An app-only update preserves calibration. A short result-screen chord reruns calibration; holding both returns. Explicit erase clears only the calibration record.

Portable checks cover geometry, sample reduction, fitting, residual interpolation, guided collection and both record formats. They do not prove physical accuracy. See [development](../DEVELOPMENT.md) for verification.
