# Incremental, archive-only VICE targets for the bare-metal BMX link.
#
# VICE's public x64/x128/... convenience targets deliberately invoke the
# corresponding *-all rule, which deletes that machine's libraries first.  BMX
# does not need the intermediate VICE executable, only its direct objects and
# the static archives named by the BMX kernel link.  The shared build helper
# invokes those archive subdirectory Makefiles explicitly so an existing
# archive never hides a changed source file from Automake's recursive rules.

.PHONY: bmx-x64 bmx-x64sc bmx-xscpu64 bmx-x128 bmx-xvic bmx-xpet bmx-xplus4

bmx-x64: $(BUILT_SOURCES) $(x64_OBJECTS)
bmx-x64sc: $(BUILT_SOURCES) $(x64sc_OBJECTS)
bmx-xscpu64: $(BUILT_SOURCES) $(xscpu64_OBJECTS)
bmx-x128: $(BUILT_SOURCES) $(x128_OBJECTS)
bmx-xvic: $(BUILT_SOURCES) $(xvic_OBJECTS)
bmx-xpet: $(BUILT_SOURCES) $(xpet_OBJECTS)
bmx-xplus4: $(BUILT_SOURCES) $(xplus4_OBJECTS)
