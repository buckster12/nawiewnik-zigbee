import {Zcl} from 'zigbee-herdsman';
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

const MANUFACTURER_CODE = 0x1234;

export default {
    zigbeeModel: ['Nawiewnik-H2'],
    model: 'Nawiewnik-H2',
    vendor: 'SantaRumor',
    description: 'ESP32-H2 native Zigbee ventilation damper',
    extend: [m.windowCovering({
        controls: ['lift'],
        configureReporting: true,
        coverInverted: false,
    }), m.battery({
        percentage: true,
        voltage: true,
        percentageReporting: false,
        voltageReporting: false,
    }), m.numeric({
        name: 'motor_speed',
        label: 'Motor speed',
        cluster: 'closuresWindowCovering',
        attribute: {ID: 0xF001, type: Zcl.DataType.UINT16},
        description: 'Stepper motor speed in half-steps per second',
        unit: 'steps/s',
        valueMin: 10,
        valueMax: 200,
        valueStep: 5,
        access: 'ALL',
        entityCategory: 'config',
        reporting: false,
        zigbeeCommandOptions: {manufacturerCode: MANUFACTURER_CODE},
    })],
};
