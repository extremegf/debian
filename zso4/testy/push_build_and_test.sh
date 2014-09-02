cd /Users/horban/Documents/workspace/kernel-3.13.3/zso4/
git add -u
git ci -m 'Autocommit'
git push
ssh root@10.4.5.65 'cd ~/linux-3.13.3/zso4/testy/; ./build_and_test.sh'
