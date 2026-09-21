"""Rebuild the bundled Chinese UI font from official Noto Sans SC.

Optional maintainer tool: pip install fonttools==4.60.1
Source: google/fonts, ofl/notosanssc/NotoSansSC[wght].ttf (OFL-1.1).
The modified subset is renamed Charger UI SC; the source OFL is bundled.
Firmware builds use the checked-in subset and require no download or fonttools.
"""
import argparse
import json
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools import subset
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
view = (ROOT / 'components/charger_display/charger_display.h').read_text()
chars = ''.join(sorted(set(chr(i) for i in range(32, 127)) | {c for c in view if ord(c) > 127}))
font = TTFont(args.source)
if 'fvar' in font:
    font = instantiateVariableFont(font, {'wght': 400}, inplace=True)
options = subset.Options()
options.name_IDs = ['*']
subsetter = subset.Subsetter(options=options)
subsetter.populate(text=chars)
subsetter.subset(font)
for record in font['name'].names:
    names = {1: 'Charger UI SC', 2: 'Regular', 3: 'ChargerUISc-Regular',
             4: 'Charger UI SC Regular', 6: 'ChargerUISc-Regular', 16: 'Charger UI SC', 17: 'Regular'}
    if record.nameID in names:
        record.string = names[record.nameID].encode(record.getEncoding())
font.save(ROOT / 'assets/ChargerSansSC.ttf')
(ROOT / 'assets/ui-glyphs.yaml').write_text(json.dumps(chars, ensure_ascii=False) + '\n')
print(f'Generated ChargerSansSC.ttf with {len(chars)} requested characters')
