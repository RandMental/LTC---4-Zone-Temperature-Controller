# Using These Prompt Files with Claude Code

## Overview

This directory contains comprehensive documentation designed to guide Claude Code in developing production-quality Arduino firmware for ESP32. These files provide context, best practices, and example prompts for common development tasks.

## File Structure

```
VictronTempMonitor/
├── README.md               # Main context document (read this first)
├── PROMPTS.md             # Example prompts for common tasks
└── .clauderc              # Configuration file for Claude Code
```

## Getting Started

### 1. First Session with Claude Code

Start every new development session by providing Claude with context:

```
I'm working on the ESP32 Victron Temperature Monitor project. 
Please read README.md for complete project context.

Today I need help with: [describe your task]
```

### 2. For Specific Tasks

Reference the appropriate section from PROMPTS.md:

```
I need to implement relay control for cooling fans.
Please refer to PROMPTS.md, section "Feature Implementation -> Add Relay Control for Cooling Fans"
and implement this feature following the project conventions.
```

### 3. For Code Review

```
Please review the current VictronTempMonitor.ino code.
Use the "Code Quality and Refactoring" prompts from PROMPTS.md as a checklist.
Focus on production readiness and error handling.
```

## Common Workflows

### Workflow 1: Adding a New Feature

**Step 1:** Find or create appropriate prompt in PROMPTS.md
```
Example: "Add Relay Control for Cooling Fans"
```

**Step 2:** Customize the prompt with your specifics
```
I need to implement relay control for cooling fans.

Hardware setup:
- Relay 1: GPIO 25 (MPPT fan) ✓
- Relay 2: GPIO 26 (MultiPlus fan) ✓

Requirements:
[paste from PROMPTS.md and modify as needed]

Additional constraints:
- Must publish fan state to MQTT
- Add manual override via Serial command

Please implement following project conventions in README.md.
```

**Step 3:** Review and test Claude's implementation
- Compile the code
- Test on hardware
- Verify against requirements

### Workflow 2: Debugging an Issue

**Step 1:** Describe the problem clearly
```
I'm having an issue with the DS18B20 sensors.

Symptoms:
- Sensor 1 reads correctly (25.3°C)
- Sensor 2 always shows -127.0°C
- Serial output shows: "Sensor 2 not detected"

Current wiring:
- Both sensors on GPIO 15
- 4.7kΩ pull-up to 3.3V
- 2-meter shielded cables

Please help diagnose and fix this issue.
Refer to README.md section "Scenario 4: Debugging Sensor Issue"
```

**Step 2:** Follow Claude's diagnostic steps

**Step 3:** Implement the solution

### Workflow 3: Code Quality Improvement

**Step 1:** Request comprehensive review
```
Please perform a production quality review of VictronTempMonitor.ino.

Use PROMPTS.md "Apply Production Best Practices" as checklist.

Priority areas:
1. Error handling
2. Memory management
3. Non-blocking code patterns
4. Documentation quality

Provide specific improvements with before/after examples.
```

**Step 2:** Implement improvements incrementally
- Fix critical issues first
- Test after each change
- Commit working code

### Workflow 4: Optimization

**Step 1:** Identify optimization target
```
The display is flickering during updates.

Please refer to PROMPTS.md "Optimize Display Performance"
and implement optimizations to reduce flicker.

Current behavior:
- Full screen clears on every update
- Visible flash when temperature changes
- Update interval is 5 seconds

Target: Smooth updates with no visible flicker
```

**Step 2:** Implement and benchmark
- Measure performance before
- Apply optimizations
- Measure performance after
- Document improvements

## Best Practices for Working with Claude Code

### DO: Provide Complete Context

**Good:**
```
I need to add humidity alerts to the system.

Context:
- Current DHT22 sensor on GPIO 27 reads humidity
- MQTT publishing works for temperature
- Home Assistant is the MQTT broker

Requirements:
- Publish humidity to "esp32/dht/ambhum" (already done)
- Add alert when humidity > 70% for > 5 minutes
- Publish alert to "victron/monitor/alert"
- Add humidity display to TFT

Please implement following patterns in README.md.
```

**Bad:**
```
Add humidity alerts
```

### DO: Reference Relevant Documentation

**Good:**
```
Please implement the relay control feature.
Refer to:
- PROMPTS.md: "Add Relay Control for Cooling Fans"
- README.md: Hardware section for GPIO pins
- README.md: Code patterns for state machines
```

**Bad:**
```
Make relays turn on when hot
```

### DO: Specify Constraints and Requirements

**Good:**
```
Implement fan control with these constraints:
- Must be non-blocking (no delay in loop)
- Add 5°C hysteresis to prevent rapid cycling
- Maximum 2-hour continuous runtime (safety)
- Publish state changes to MQTT
- Allow manual override via Serial command
- Follow error handling patterns from README.md
```

**Bad:**
```
Add fans that turn on when it's hot
```

### DO: Ask for Explanations

**Good:**
```
Please implement the watchdog timer feature from PROMPTS.md.

After implementation:
1. Explain why the watchdog timeout is set to 30 seconds
2. Describe what happens if the watchdog triggers
3. Show how to test the watchdog without waiting 30 seconds
4. Explain the strategic feeding points in the code
```

**Bad:**
```
Add watchdog
```

### DON'T: Assume Claude Remembers Previous Sessions

Each session is fresh. Always provide context:

**Good:**
```
Continuing work on ESP32 Victron Temperature Monitor.
Context: README.md
Previous session: Implemented relay control
Today: Add relay status to MQTT and display

[provide details]
```

**Bad:**
```
Continue where we left off yesterday with the relays
```

### DON'T: Skip Testing Steps

Always verify Claude's implementations:

**Required Checks:**
- [ ] Code compiles without errors
- [ ] Memory usage is acceptable
- [ ] Functionality works as specified
- [ ] No regressions in existing features
- [ ] Serial output shows expected messages
- [ ] Long-term stability (run for hours)

## Example Session Transcript

Here's an example of an effective Claude Code session:

```
USER:
I'm working on the ESP32 Victron Temperature Monitor.
Please read README.md for project context.

I need to add a relay control system for cooling fans.
Refer to PROMPTS.md section "Feature Implementation -> Add Relay Control".

Specific requirements:
- Relay 1 (GPIO 25): Controls MPPT fan
- Relay 2 (GPIO 26): Controls MultiPlus fan
- Turn on at 50°C, turn off at 45°C (5° hysteresis)
- Publish state to MQTT (ON/OFF)
- Add manual control via Serial commands
- Implement safety timeout (2 hours max)

Please provide complete implementation following patterns in README.md.

---

CLAUDE:
[reads README.md and PROMPTS.md]

I'll implement the relay control system with the following components:

1. State machine for each relay
2. Hysteresis logic to prevent rapid cycling
3. MQTT status publishing
4. Serial command interface
5. Safety timeout mechanism

Here's the implementation:

[provides code with detailed comments]

---

USER:
This looks good. Two questions:

1. Should the safety timeout reset if temperature drops below 45°C?
2. What happens if MQTT publish fails - does relay still operate?

---

CLAUDE:
Good questions! Here's the clarification:

1. Yes, the timeout should reset when the fan turns off (temp < 45°C)...
[explains logic]

2. Yes, relay operates independently of MQTT status...
[explains fail-safe behavior]

Updated code:
[provides corrected version]

---

USER:
Perfect. Compiling and testing now.

[tests code]

Works great! Please add this to a new function called updateFanControl()
and integrate it into the main loop following the non-blocking pattern
from README.md section "Code Patterns".

---

CLAUDE:
Here's the refactored version with updateFanControl():
[provides clean integration]

This follows the non-blocking pattern and integrates with existing timing.

---

USER:
Excellent. Final request: Add documentation comments in Doxygen style
as shown in PROMPTS.md "Generate API Documentation".

---

CLAUDE:
[provides fully documented code with Doxygen comments]
```

## Tips for Maximum Productivity

### 1. Keep README.md and PROMPTS.md Updated
When you add new features or discover new patterns, update the documentation:
- Add new examples to PROMPTS.md
- Update hardware specs in README.md
- Document lessons learned

### 2. Create Project-Specific Prompts
PROMPTS.md provides templates. Customize them for your needs:
```
## Project-Specific Prompts

### Add Battery Voltage Monitoring
[your custom prompt based on templates]

### Integrate with Solar Charge Controller
[your custom prompt]
```

### 3. Use .clauderc for Quick Reference
The .clauderc file contains hardware specs and constraints. Reference it:
```
Please add the relay control on the GPIOs specified in .clauderc
```

### 4. Build a Library of Tested Solutions
When Claude provides a good solution:
1. Test it thoroughly
2. Add it to PROMPTS.md as an example
3. Include in README.md patterns section
4. Commit to version control

### 5. Progressive Enhancement
Don't try to do everything at once:

**Phase 1:** Basic functionality
- Sensor reading
- Display output
- MQTT publishing

**Phase 2:** Reliability
- Error handling
- Watchdog timer
- Auto-reconnection

**Phase 3:** Advanced features
- Web interface
- OTA updates
- Data logging

**Phase 4:** Optimization
- Performance tuning
- Memory optimization
- Power saving

## Troubleshooting Claude Code Sessions

### Issue: Claude doesn't follow project conventions

**Solution:**
```
Please review README.md section "Production Code Requirements"
and "Critical Don'ts" before implementing.

Specific convention to follow:
[quote the relevant section]
```

### Issue: Claude provides code that doesn't compile

**Solution:**
```
The code doesn't compile. Error message:

[paste error]

Please review:
1. Library includes at top of file
2. Variable declarations
3. Function signatures

Refer to current working code in VictronTempMonitor.ino
```

### Issue: Claude's solution is too complex

**Solution:**
```
This solution is more complex than needed.

Requirements:
- Simple on/off relay control (not PID)
- Basic hysteresis (not advanced algorithms)
- Direct GPIO control (not external libraries)

Please simplify following the "keep it simple" principle in README.md
```

### Issue: Code doesn't match existing style

**Solution:**
```
Please refactor to match existing code style:
- Use millis() timing like other intervals in loop()
- Follow error handling pattern from readSensorWithRetry()
- Use same comment style as displayDevice()

Reference: README.md "Code Patterns and Examples"
```

## Getting Help

If you're stuck or unsure how to phrase a prompt:

1. **Review PROMPTS.md** - Find similar examples
2. **Check README.md** - Review relevant patterns
3. **Start Simple** - Get basic functionality first
4. **Be Specific** - Clear requirements = better code
5. **Iterate** - Refine based on results

## Contributing to These Documents

As you use these prompt files, you'll discover:
- Better ways to phrase prompts
- New patterns that work well
- Common issues and solutions

Please update:
- PROMPTS.md with new examples
- README.md with new patterns
- This guide with workflow improvements

---

**Last Updated:** January 2025  
**Maintainer:** [Your Name]  
**Questions:** [Contact Information]
