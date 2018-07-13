echo "Building nxlink"
cd sys-nxlink
make clean && make
mv sys-nxlink.kip ../sys-nxlink.kip
cd ..

echo "hbloader nxlink"
cd nx-hbloader
make clean && make
cp hbl.nso ../main
cp hbl.npdm ../main.npdm
echo "Build complete"
echo "Copy main and main.npdm to sdmc:/atmosphere/titles/010000000000100D/exefs/"
echo "Copy sys-nxlink.kip to sdmc:/modules/"
echo "Edit hekate_ipl.ini and add"
echo "[NXLINK + CFW]"
echo "kip1=modules/sys-nxlink.kip"
echo "kip1=modules/newfirm/loader.kip"
echo "kip1=modules/newfirm/sm.kip"
echo "as a new section"