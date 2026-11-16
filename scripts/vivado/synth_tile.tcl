# Out-of-context Vivado synthesis of one Chipyard tile; reports its area.
#
# Usage (Windows cmd/PowerShell or Linux, from the repo root):
#   vivado -mode batch -source scripts/vivado/synth_tile.tcl -tclargs \
#       <verilog_dir> <top_module> <label> [part]
#
#   verilog_dir  folder holding the tile's .sv/.v files (a copy of the config's
#                generated-src/.../gen-collateral folder)
#   top_module   RocketTile or BoomTile (check the exact name, see the guide)
#   label        a name for this run, e.g. BudgetR1Config
#   part         default xc7a200tsbg484-1 (Artix-7 200T, free Vivado license).
#                Use the SAME part for every tile.
#
# Writes results/synth/<label>/ (utilization reports, log) and appends a row to
# data/tile_area.csv: config,top,part,luts,ffs,bram_tiles,dsps
#
# Files that use DPI-C or are simulation-only (TestHarness, Sim*) are skipped:
# the tile does not use them and Vivado cannot parse DPI.

if {[llength $argv] < 3} {
    puts "usage: -tclargs <verilog_dir> <top_module> <label> \[part\]"
    exit 1
}
set src_dir [file normalize [lindex $argv 0]]
set top     [lindex $argv 1]
set label   [lindex $argv 2]
set part    [expr {[llength $argv] > 3 ? [lindex $argv 3] : "xc7a200tsbg484-1"}]

set out_dir [file normalize "results/synth/$label"]
file mkdir $out_dir
file mkdir data

set n_read 0
set skipped {}
foreach f [lsort [concat [glob -nocomplain -directory $src_dir *.sv] \
                         [glob -nocomplain -directory $src_dir *.v]]] {
    set name [file tail $f]
    if {[string match "TestHarness*" $name] || [string match "Sim*" $name]} {
        lappend skipped $name; continue
    }
    set fh [open $f r]; set body [read $fh]; close $fh
    if {[string first "DPI-C" $body] >= 0} { lappend skipped $name; continue }
    if {[string match "*.sv" $name]} { read_verilog -sv $f } else { read_verilog $f }
    incr n_read
}
puts "Read $n_read files; skipped [llength $skipped]: $skipped"

# Header files (.svh/.vh) included by the sources
set_property include_dirs [list $src_dir] [current_fileset]

synth_design -top $top -part $part -mode out_of_context -flatten_hierarchy rebuilt

report_utilization -file "$out_dir/utilization.rpt"
report_utilization -hierarchical -hierarchical_depth 4 -file "$out_dir/utilization_hier.rpt"

set rpt [report_utilization -return_string]
proc grab {rpt pattern} {
    if {[regexp -line "^\\|\\s*${pattern}\\s*\\|\\s*(\[0-9.\]+)" $rpt -> v]} { return $v }
    return "NA"
}
set luts [grab $rpt {(?:Slice|CLB) LUTs\*?}]
set ffs  [grab $rpt {(?:Slice|CLB) Registers}]
set bram [grab $rpt {Block RAM Tile}]
set dsps [grab $rpt {DSPs}]

set csv "data/tile_area.csv"
set new [expr {![file exists $csv]}]
set fh [open $csv a]
if {$new} { puts $fh "config,top,part,luts,ffs,bram_tiles,dsps" }
puts $fh "$label,$top,$part,$luts,$ffs,$bram,$dsps"
close $fh

puts "=================================================="
puts " $label / $top on $part"
puts " LUTs $luts   FFs $ffs   BRAM tiles $bram   DSPs $dsps"
puts " Reports: $out_dir"
puts "=================================================="
