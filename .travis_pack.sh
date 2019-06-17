rm -rf __install
mkdir __install

echo ${TRAVIS_JOB_ID} > __install/version
echo ${TRAVIS_JOB_NUMBER} >> __install/version

# Copy files
cp -r lualib __install/lualib
cp 3rd/librs232/bindings/lua/rs232.lua __install/lualib
cp -r luaclib __install/luaclib
cp -r service __install/service
cp -r cservice __install/cservice
cp README.md __install/
cp HISTORY.md __install/
cp LICENSE __install/
cp skynet __install/

cd __install/

ln -s ../freeioe ./ioe
ln -s /var/log ./logs
cd - > /dev/null

cd __install
tar czvf ../skynet_release.tar.gz * > /dev/null
md5sum ../skynet_release.tar.gz > ../skynet_release.tar.gz.md5
