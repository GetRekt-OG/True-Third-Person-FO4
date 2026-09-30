"""Validate the shipped MCM controls, defaults and plugin setting bindings."""
from pathlib import Path
import configparser
import json

root = Path(__file__).resolve().parents[1]
folder = root / 'package/Data/MCM/Config/TrueThirdPerson'
config = json.loads((folder / 'config.json').read_text())
defaults = configparser.ConfigParser()
defaults.optionxform = str
defaults.read(folder / 'settings.ini')
source = ''.join(path.read_text() for path in sorted((root / 'src').glob('*.hpp')))
logic = (root / 'src/Logic.hpp').read_text()
key_lists = {name: logic.split(f'Key {name}[]{{', 1)[1].split('};', 1)[0].count('{Device::')
             for name in ('keyboardMouse', 'gamepad')}
controls = config['content'] + [control for page in config['pages'] for control in page['content']]
seen = set()
for control in controls:
    if control['type'] in ('text', 'section', 'spacer'):
        assert 'id' not in control and control.get('text', '') != '' or control['type'] == 'spacer'
        continue
    identifier = control['id']
    assert identifier not in seen, identifier
    seen.add(identifier)
    key, section = identifier.split(':')
    assert defaults.has_option(section, key), identifier
    assert f'L"{key}"' in source, identifier
    options = control['valueOptions']
    value = float(defaults[section][key])
    if control['type'] == 'switcher':
        assert key.startswith('b') and options['sourceType'] == 'ModSettingBool'
        assert value in (0, 1)
    elif control['type'] == 'dropdown':
        assert key.startswith('i') and options['sourceType'] == 'ModSettingInt'
        expected = key_lists['gamepad' if key == 'iGamepadKey' else 'keyboardMouse']
        assert len(options['options']) == expected, identifier
        assert options['options'][-1] == 'None' and value == 0
    else:
        assert control['type'] == 'slider'
        assert options['sourceType'] == ('ModSettingFloat' if key.startswith('f') else 'ModSettingInt')
        assert options['min'] <= value <= options['max'] and options['step'] > 0
assert seen == {f'{key}:{section}' for section in defaults.sections() for key in defaults[section]}
assert defaults['Main']['bUseMCM'] == '0'
assert defaults['TargetLock']['bShowMarker'] == '1'
assert defaults['RangedTargetLock']['bEnabled'] == '0'
assert defaults['Main']['bResetDefaults'] == '0'
assert 'ShowMarker=1' in (root / 'package/Data/F4SE/Plugins/TrueThirdPerson.ini').read_text()
assert 'install(DIRECTORY package/Data/MCM/' in (root / 'CMakeLists.txt').read_text()
assert not (root / 'package/Data/MCM/Settings/TrueThirdPerson.ini').exists()
print(f'{len(seen)} MCM settings validated')
