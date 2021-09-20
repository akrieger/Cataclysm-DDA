rm -Recurse -Force bindist

mkdir bindist
cp Cataclysm-vcpkg-static-Release-x64.exe bindist/cataclysm-tiles.exe
cp tools/format/JsonFormatter-vcpkg-static-Release-x64.exe bindist/json_formatter.exe

mkdir bindist/lang
cp -r lang/mo bindist/lang

$extras = "data", "doc", "gfx", "LICENSE.txt", "LICENSE-OFL-Terminus-Font.txt", "README.md", "VERSION.txt"
ForEach ($extra in $extras) {
	cp -r $extra bindist
}
