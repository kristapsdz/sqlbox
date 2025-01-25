#! /bin/sh

if [ -f Makefile.local ]
then
	mv Makefile.local Makefile.local~
fi
bmake clean
CFLAGS="--coverage" ./configure LDFLAGS="--coverage"
bmake regress
( echo '<?xml version="1.0" encoding="UTF-8" ?>'; \
  echo '<article data-sblg-article="1" data-sblg-tags="coverage">'; \
  echo '<aside>'; \
  echo '<div class="coverage-table">' ; \
  for f in *.o ; do \
	[ $f = "compats.o" ] && continue ; \
	src=$(basename $f .o).c ; \
	link=https://github.com/kristapsdz/sqlbox/blob/master/$src ; \
	pct=$(gcov -n -H $src | grep 'Lines executed' | head -n1 | \
		cut -d ":" -f 2 | cut -d "%" -f 1) ; \
	echo "<a href=\"$link\">$src</a><span>$pct%</span>" ; \
	echo "$src: $pct%" 1>&2 ; \
  done ; \
  echo "</div>"; \
  echo "</aside>"; \
  echo "</article>"; \
) >coverage.xml
if [ -f Makefile.local~ ]
then
	mv Makefile.local~ Makefile.local
fi
