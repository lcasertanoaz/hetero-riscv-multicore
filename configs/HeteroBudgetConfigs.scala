// Heterogeneous Rocket + BOOM SoC configurations for a fixed FPGA resource budget.
//
// HOW TO USE
//   1. Copy this file next to the course config that builds SmallBOOM for you
//      (default Chipyard location: generators/chipyard/src/main/scala/config/).
//      If the course keeps configs elsewhere, change the `package` line to match
//      the files already there.
//   2. Check the fragment names against your Chipyard version:
//        grep -rn "class CourseRocketConfig" -A8 $CHIPYARD_DIR
//        grep -rn "class WithNHugeCores"     $CHIPYARD_DIR/generators/rocket-chip/src
//        grep -rn "class WithNSmallBooms"    $CHIPYARD_DIR/generators/boom/src
//        grep -rn "case class RocketTileAttachParams" $CHIPYARD_DIR/generators/rocket-chip/src
//        grep -rn "case class BoomTileAttachParams"   $CHIPYARD_DIR/generators/boom/src
//      Fix the imports below if a package differs.
//   3. The 1-core configs are used for Vivado synthesis and single-core runs.
//      The sweep configs at the bottom are PLACEHOLDERS until synthesis sets the
//      core counts (scripts/budget.py).
//
// Every config uses the same L2, memory system and 128-bit system bus, so
// differences come from the cores alone.

package chipyard

import org.chipsalliance.cde.config.Config
import freechips.rocketchip.subsystem.{TilesLocated, InSubsystem}
import freechips.rocketchip.subsystem.RocketTileAttachParams
import boom.v3.common.BoomTileAttachParams              // older Chipyard: boom.common.BoomTileAttachParams

// ---------------- Performance counters on every core ----------------
// Rocket-chip's own WithNPerfCounters only changes Rocket tiles. This sets the
// number of programmable counters (mhpmcounter3..) on Rocket AND BOOM tiles.
// It must sit LEFT of the core fragments so it sees the tiles they add.
class WithHeteroPerfCounters(n: Int = 4) extends Config((site, here, up) => {
  case TilesLocated(InSubsystem) => up(TilesLocated(InSubsystem), site) map {
    case tp: RocketTileAttachParams => tp.copy(tileParams = tp.tileParams.copy(
      core = tp.tileParams.core.copy(nPerfCounters = n)))
    case tp: BoomTileAttachParams => tp.copy(tileParams = tp.tileParams.copy(
      core = tp.tileParams.core.copy(nPerfCounters = n)))
    case other => other
  }
})

// ---------------- 1-core configs: synthesis + single-core runs ----------------

class BudgetR1Config extends Config(
  new WithHeteroPerfCounters(4) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++     // match CourseRocketConfig's core fragment
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

class BudgetSB1Config extends Config(
  new WithHeteroPerfCounters(4) ++
  new boom.v3.common.WithNSmallBooms(1) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

class BudgetMB1Config extends Config(
  new WithHeteroPerfCounters(4) ++
  new boom.v3.common.WithNMediumBooms(1) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

// ---------------- Sweep configs (PLACEHOLDER counts) ----------------
// Replace the counts with what budget.py prints for your synthesis numbers.
// Rename to match, e.g. BudgetSB1R3Config = 1 SmallBOOM + 3 Rocket.

class BudgetR4Config extends Config(                          // Rocket-only
  new WithHeteroPerfCounters(4) ++
  new freechips.rocketchip.rocket.WithNHugeCores(4) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

class BudgetSB1R2Config extends Config(                       // mixed
  new WithHeteroPerfCounters(4) ++
  new boom.v3.common.WithNSmallBooms(1) ++
  new freechips.rocketchip.rocket.WithNHugeCores(2) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

class BudgetSB2Config extends Config(                         // BOOM-only
  new WithHeteroPerfCounters(4) ++
  new boom.v3.common.WithNSmallBooms(2) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)

class BudgetMB1R1Config extends Config(                       // 562 extension
  new WithHeteroPerfCounters(4) ++
  new boom.v3.common.WithNMediumBooms(1) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)
