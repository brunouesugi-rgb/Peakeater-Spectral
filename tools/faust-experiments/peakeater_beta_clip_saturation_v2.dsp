import("stdfaust.lib");

driveDb = hslider("drive[dB]", 6.0, 0.0, 48.0, 0.1);
sat = hslider("saturation", 0.25, 0.0, 1.0, 0.001);
clip = hslider("clip", 0.25, 0.0, 1.0, 0.001);
ceilingDb = hslider("ceiling[dB]", -0.1, -12.0, 0.0, 0.1);

drive = ba.db2linear(driveDb);
ceiling = ba.db2linear(ceilingDb);

softclip(x, amount) = ma.tanh(x * (1.0 + amount * 4.2)) / ma.tanh(1.0 + amount * 4.2);
roundclip(x, amount) = (atan(x * (1.0 + amount * 5.6)) / atan(1.0 + amount * 5.6));
cubicdensity(x, amount) = x - (x * x * x * amount * 0.035);

saturate(x) = x + (((softclip(x, sat) * 0.52) + (roundclip(x, sat) * 0.28) + (cubicdensity(x, sat) * 0.20)) - x) * sat;
limitclip(x) = x + (((softclip(x / ceiling, clip) * 0.64) + (roundclip(x / ceiling, clip) * 0.36)) * ceiling - x) * clip;

process = _ <: (*(drive) : saturate : limitclip), (*(drive) : saturate : limitclip);
