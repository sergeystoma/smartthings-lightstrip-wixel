/**
 *  Wixel Lightstrip
 *
 *  Copyright 2016 Misty View
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 *  in compliance with the License. You may obtain a copy of the License at:
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
 *  on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License
 *  for the specific language governing permissions and limitations under the License.
 *
 */
metadata {
	definition (name: "Wixel Lightstrip", namespace: "sergeystoma", author: "Misty View") {
		capability "Color Control"
        capability "Switch"
        
        command "setLevel"
	}

	simulator {
	}

	tiles(scale: 2) {
		multiAttributeTile(name:"rich-control", type: "lighting", width: 6, height: 4, canChangeIcon: false) {
            tileAttribute ("device.switch", key: "PRIMARY_CONTROL") {
                attributeState "on", label: "ON", action: "switch.off", icon: "st.lights.multi-light-bulb-on", backgroundColor: "#79b821", nextState: "off"
                attributeState "off", label: "OFF", action: "switch.on", icon: "st.lights.multi-light-bulb-off", backgroundColor: "#ffffff", nextState: "on"
            }
            tileAttribute ("device.level", key: "SLIDER_CONTROL") {
                attributeState "level", action: "setLevel", range:"(0..100)"
            }
            tileAttribute ("device.level", key: "SECONDARY_CONTROL") {
                attributeState "level", label: '${currentValue}%'
            }
            tileAttribute ("device.color", key: "COLOR_CONTROL") { 
                attributeState "color", action: "setColor"
            }
        }
        
        main("rich-control")
        details(["rich-control"])
	}
}

def on() {
    sendEvent(name: "switch", value: "on");
    
    updateColor();
}

def off() {
    sendEvent(name: "switch", value: "off");
    
    updateColor();
}

def parse(String description) {
}

def setHue(value) {
    sendEvent(name: "hue", value: value)
    
    updateColor();
}

def setSaturation(value) {
    sendEvent(name: "saturation", value: value)
    
    updateColor();
}

def setLevel(value) {
    sendEvent(name: "level", value: value);
    
    updateColor();
}

def setColor(value) {
    if (value.hue) { sendEvent(name: "hue", value: value.hue)}
    if (value.saturation) { sendEvent(name: "saturation", value: value.saturation)}
    if (value.hex) { sendEvent(name: "color", value: value.hex)}
        
	updateColor();    
}

def clamp(v, min, max) {
	if (v < min) {
    	return min;
    }
    if (v > max) {
    	return max;
    }
    return v;
}

def updateColor() {
    def h = device.currentValue("hue") as int;
    def s = device.currentValue("saturation") as int;
    def level = device.currentValue("level") as int;
    def on = device.currentValue("switch");
    
    def v;
    if (on == "on") {
    	v = (level * 255 / 100) as int;
	} else {
    	v = 0;
    }
    
    h = ((h * 239) / 100) as int;
    s = ((s * 255) / 100) as int;
    
    h = clamp(h, 0, 239);
    s = clamp(s, 0, 255);
    v = clamp(v, 0, 255);
    
    def colr = "colr $h $s $v";
    
    def msg = zigbee.smartShield(text: colr).format();
    
    return msg;
}