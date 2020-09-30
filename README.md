# CoffeeBot
An Arduino based coffee device

# How to start
- Install platformIO IDE for VScode
- Open project
- Build
- Upload

# EEPROM contents

### Header: (22 x uint8)
    Latest position of log (uint16)
    Weight ratio to get cl (float32)
    Weight diff to get cl (float32)
    All time coffee wasted cl (float32)
    All time coffee used in cl (float32)
    All time coffee brewed in cl (float32)
### Current usage estimate: 7 x 24 x 2 (336x 3x uint8)
    times this spot saved to (uint8)
    coffee used in cl (uint16)
### Log (Remaining space)
    Time of log ( )
    Coffee wasted in cl(uint8)
    Coffee used in cl (uint8)
    Coffee brewed in cl (uint8)
