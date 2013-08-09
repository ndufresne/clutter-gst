/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

const Lang = imports.lang;
const Mainloop = imports.mainloop;
const Gettext = imports.gettext;
const _ = imports.gettext.gettext;

const Clutter = imports.gi.Clutter;
const ClutterGst = imports.gi.ClutterGst;

const ANIMATION_MS = 300;

Clutter.init(null, null);
ClutterGst.init(null, null);

let stage = new Clutter.Stage({
    width: 200,
    height: 600,
    layout_manager: new Clutter.BoxLayout({
        orientation: Clutter.Orientation.VERTICAL,
        pack_start: false,
        use_animations: true,
        spacing: 10,
        homogeneous: true,
        easing_duration: ANIMATION_MS,
        easing_mode: Clutter.AnimationMode.LINEAR,
    }),
    user_resizable: true,
    background_color: new Clutter.Color({
        red: 0, green: 0, blue: 0, alpha: 0xff,
    }),
});
stage.connect('destroy', Clutter.main_quit);


let players = [];

let addCamera = function(device) {
    log('adding camera : ' + device.get_name());
    let player = new ClutterGst.Camera({
        device: device,
    });
    player.connect('ready', Lang.bind(this, function() {
        let actor = new Clutter.Actor({
            content: new ClutterGst.Aspectratio({
                player: player,
            }),
            width: 200,
            height: 200,
            scale_x: 0,
            scale_y: 0,
            pivot_point: new Clutter.Point({
                x: 0.5,
                y: 0.5,
            }),
        });
        stage.add_child(actor);

        actor.save_easing_state();
        actor.set_easing_duration(ANIMATION_MS);
        actor.set_easing_mode(Clutter.AnimationMode.LINEAR);
        actor.scale_x = actor.scale_y = 1;
        actor.restore_easing_state();
    }));
    player.set_playing(true);
};

let removeCamera = function(device) {
    let children = stage.get_children();
    for (let i in children) {
        let actor = children[i];
        if (actor.content.player.device == device) {
            actor.save_easing_state();
            actor.set_easing_duration(ANIMATION_MS)
            actor.set_easing_mode(Clutter.AnimationMode.LINEAR);
            actor.scale_x = actor.scale_y = 0;
            actor.restore_easing_state();

            let timeline = actor.get_transition('scale-x');
            timeline.connect('completed', Lang.bind(this, function() {
                log('actor removed');
                stage.remove_child(actor);
            }));
            break;
        }
    }
};


let cameraManager = ClutterGst.CameraManager.get_default();
cameraManager.connect('camera-added', Lang.bind(this, function(manager, cameraDevice) {
    addCamera(cameraDevice);
}));
cameraManager.connect('camera-removed', Lang.bind(this, function(manager, cameraDevice) {
    removeCamera(cameraDevice);
}));

let devices = cameraManager.get_camera_devices();
for (let i in devices)
    addCamera(devices[i]);

stage.show();
Clutter.main();
