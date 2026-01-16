#
# mmu.library -gcc preparation
#
# Copyright (C) 2026 Matthias Heinrichs
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

SRCP      = src
INCP      = include
LOCP	  = locale
REFP      = reference
DSTP      = distribution
OBJP      = build
RELP      = release
DOCP      = docs



.PHONY : all clean source release check pack

all: $(OBJP) \
		$(REFP)/inline/mmu_protos.h \
		$(REFP)/inline/mmu.h \
		$(REFP)/proto/mmu.h \

clean:
	rm -rf $(OBJP) $(RELP) $(REFP)/inline/mmu_protos.h $(REFP)/inline/mmu.h \
		$(REFP)

#-- pragmas
$(REFP)/inline/mmu_protos.h: $(REFP)/fd/mmu_lib.fd $(REFP)/clib/mmu_protos.h
	mkdir -p $(REFP)/inline/
	fd2pragma --infile $(REFP)/fd/mmu_lib.fd --clib $(REFP)/clib/mmu_protos.h \
		--to $(REFP)/inline/ --special 70 --autoheader --comment

$(REFP)/inline/mmu.h: $(REFP)/fd/mmu_lib.fd $(REFP)/clib/mmu_protos.h
	mkdir -p $(REFP)/inline/
	fd2pragma --infile $(REFP)/fd/mmu_lib.fd --clib $(REFP)/clib/mmu_protos.h \
		--to $(REFP)/inline/ --special 40 --autoheader --comment

$(REFP)/proto/mmu.h: $(REFP)/fd/mmu_lib.fd $(REFP)/clib/mmu_protos.h
	mkdir -p $(REFP)/proto/
	fd2pragma --infile $(REFP)/fd/mmu_lib.fd --clib $(REFP)/clib/mmu_protos.h \
		--to $(REFP)/proto/ --special 38 --autoheader --comment