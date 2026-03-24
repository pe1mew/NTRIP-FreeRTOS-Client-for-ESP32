### Example JSON payload

```json
{
  "seq": 4,
  "tim": "17:30:20.000",
  "vhl": { "thr":{"min":0,"max":0,"avg":0, }, "spd":{"min":0,"max":0,"avg":0, } },
  "mtr": { "pwr": 250, "rpm": 200, "trq": 55 },
  "spc": { "vsc": 55.32, "fsa": 2, "tankP": 250.3}
}
```

spd = speed m/s 

thr = 0-100% throttle newton/meters, padle position woth cruise control.
fsa => | Fuel Cell Current | FC_A | A | e.g. 10.21 A | `float` |