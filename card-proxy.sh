export YBORM_URL=sqlite+sqlite:///home/skydreamer/workspace/test_db.sqlite
make
cppcheck -q -j4 --enable=performance,portability,warning,style ./ 2> cppcheck_carp_proxy.out
valgrind ./src/card_proxy