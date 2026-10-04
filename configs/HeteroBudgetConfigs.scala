// Heterogeneous Rocket + BOOM SoC configurations for a fixed FPGA resource budget.
//
// HOW TO USE
//   1. Copy this file into the course's student-work/ config directory.
//   2. Match the core fragments to your Chipyard version. Package names moved
//      between releases, so copy them from the configs that already build in
//      the course environment:
//        grep -rn "class SmallBoomV3Config"  generators/
//        grep -rn "class CourseRocketConfig" .
//        see generators/chipyard/src/main/scala/config/HeteroConfigs.scala
//   3. Set the core counts from the Vivado LUT budget (scripts/budget.py).
//
// All configs share AbstractConfig's L2 and memory system, so differences come
// from the cores alone. Core counts below are PLACEHOLDERS.

package chipyard

import org.chipsalliance.cde.config.Config

// ---------------- Homogeneous baselines ----------------

class BudgetRocketOnlyConfig extends Config(
  new freechips.rocketchip.rocket.WithNHugeCores(4) ++        // TODO: count from budget.py
  new chipyard.config.AbstractConfig)

class BudgetSmallBoomOnlyConfig extends Config(
  new boom.v3.common.WithNSmallBooms(1) ++                     // TODO: count from budget.py
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

// ---------------- Heterogeneous mixes (same budget) ----------------

class Budget1SmallBoom2RocketConfig extends Config(
  new boom.v3.common.WithNSmallBooms(1) ++
  new freechips.rocketchip.rocket.WithNHugeCores(2) ++         // TODO
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

// ---------------- Extension: Medium BOOM as the big core ----------------

class Budget1MediumBoomNRocketConfig extends Config(
  new boom.v3.common.WithNMediumBooms(1) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++         // TODO
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)
