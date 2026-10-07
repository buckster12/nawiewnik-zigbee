from pathlib import Path
import re
import subprocess
import tempfile
import unittest

SOURCE = (Path(__file__).parents[1] / 'main' / 'battery_monitor.c').read_text()

def define(name):
    match = re.search(r'^#define ' + name + r' (\S+)', SOURCE, re.M)
    assert match is not None, 'Missing production define: ' + name
    return match[1]

class BatteryDividerTests(unittest.TestCase):
    def test_adc_range_matches_as_soldered_divider(self):
        self.assertEqual(define('BATTERY_ADC_ATTEN'), 'ADC_ATTEN_DB_12')

    def test_real_conversion_expression_uses_as_soldered_resistors(self):
        # Compile the actual production C expression, not an independent copy.
        match = re.search(r'uint32_t cell_mv = (.*?);', SOURCE, re.S)
        assert match is not None, 'Missing production conversion expression'
        expression = match[1]
        constants = '\n'.join('#define '+name+' '+define(name) for name in ['BATTERY_DIVIDER_TOP_OHM','BATTERY_DIVIDER_BOTTOM_OHM'])
        c = '#include <stdint.h>\n#include <stdio.h>\n' + constants + '\nint main(void) { int inputs[]={2506,3038,3190}; for(int i=0;i<3;i++){ int adc_mv=inputs[i]; uint32_t cell_mv='+expression+'; printf("%u\\n",cell_mv); } return 0; }\n'
        with tempfile.TemporaryDirectory(prefix='battery-divider-') as folder:
            p=Path(folder);(p/'test.c').write_text(c)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
            values=[int(v) for v in subprocess.check_output([str(p/'test')],text=True).splitlines()]
        for actual,expected in zip(values,[3300,4000,4200]):
            self.assertLessEqual(abs(actual-expected),1)

if __name__=='__main__':
    unittest.main()
