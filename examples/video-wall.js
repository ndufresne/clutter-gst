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
const Tweener = imports.tweener.tweener;

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const Clutter = imports.gi.Clutter;
const ClutterGst = imports.gi.ClutterGst;
const Cairo = imports.cairo;
const Mx = imports.gi.Mx;

const COLUMNS = 3;
const ROWS = 3;

if (ARGV.length < 1)
    throw "Need 1 argument : video-wall.js videofile";

Clutter.init(null, null);
ClutterGst.init(null, null);


let stage = new Clutter.Stage();
stage.set_background_color(new Clutter.Color({ red: 0,
                                               green: 0,
                                               blue: 0,
                                               alpha: 0xff }));
stage.connect('destroy',
              Lang.bind(this, function() { Clutter.main_quit(); }));

player = new ClutterGst.Playback();
player.set_filename(ARGV[0]);
player.set_audio_volume(0);
player.set_progress(0.20);

let actors = [];

let repositionActor = function(actorId, duration) {
    let actor = actors[actorId % actors.length];
    let subActor = actor.get_child_at_index(0);

    let stageWidth = stage.get_width();
    let stageHeight = stage.get_height();

    actor.x = stageWidth / 2 - actor.width / 2;
    actor.y = stageHeight / 2 - actor.height / 2;

    let initialYRotation = 45.0;
    let initialXRotation = -45.0;

    actor.get_parent().set_child_below_sibling(actor, null);

    actor.save_easing_state();
    actor.set_easing_duration(duration);
    actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_CUBIC);

    subActor.save_easing_state();
    subActor.set_easing_duration(duration);
    subActor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_CUBIC);

    actor.translation_x = 0;
    actor.translation_y = 0;

    actor.set_rotation_angle(Clutter.RotateAxis.Y_AXIS,
                             initialYRotation - 90 * actor._myXPos / (COLUMNS - 1));
    actor.set_rotation_angle(Clutter.RotateAxis.X_AXIS,
                             initialXRotation + 90 * actor._myYPos / (ROWS - 1));
    subActor.translation_z = -100 * (1 + COLUMNS);

    subActor.restore_easing_state();
    actor.restore_easing_state();
};

let pushActorToFront = function(actorId, duration) {
    let actor = actors[actorId % actors.length];
    let subActor = actor.get_child_at_index(0);

    let stageWidth = stage.get_width();
    let stageHeight = stage.get_height();

    actor.save_easing_state();
    actor.set_easing_duration(duration);
    actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_CUBIC);

    subActor.save_easing_state();
    subActor.set_easing_duration(duration);
    subActor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_CUBIC);

    actor.set_rotation_angle(Clutter.RotateAxis.Y_AXIS, 0);
    actor.set_rotation_angle(Clutter.RotateAxis.X_AXIS, 0);

    actor.translation_x = actor._myXPos * actor.width +
        (stageWidth / 2) - (COLUMNS * actor.width) / 2 - actor.x;
    actor.translation_y = actor._myYPos * actor.height +
        (stageHeight / 2) - (ROWS * actor.height) / 2 - actor.y;
    subActor.translation_z = -50;

    subActor.restore_easing_state();
    actor.restore_easing_state();
};

let repositionActors = function(duration) {
    for (let i = 0; i < actors.length; i++) {
        repositionActor(i, duration);
    }
};

let pushActorsToFront = function(duration) {
    for (let i = 0; i < actors.length; i++) {
        pushActorToFront(i, duration);
    }
};


for (let i = 0; i < ROWS; i++) {
    for (let j = 0; j < COLUMNS; j++) {
        let input = new ClutterGst.Box({ x1: j / COLUMNS,
                                         x2: (j + 1) / COLUMNS,
                                         y1: i / ROWS,
                                         y2: (i + 1) / ROWS,
                                       })
        let subActor = new ClutterGst.Crop({ width: 200,
                                             height: 200,
                                             player: player,
                                             input_region: input,
                                           });
        let actor = new Clutter.Actor();
        actor.add_child(subActor);

        actor._myXPos = j;
        actor._myYPos = i;

        actor.set_pivot_point(actor._myXPos / (COLUMNS - 1),
                              actor._myYPos / (ROWS - 1));

        stage.add_child(actor);

        actors.push(actor);
    }
}

stage.connect('allocation-changed', Lang.bind(this, function() {
    pushActorsToFront(0);
}));
stage.set_user_resizable(true);

player.set_playing(true);

const ANIM_TIMEOUT = 1000;

let inv = false;
Mainloop.timeout_add(ANIM_TIMEOUT + 500, Lang.bind(this, function() {
    inv ? repositionActors(ANIM_TIMEOUT) : pushActorsToFront(ANIM_TIMEOUT);
    inv = !inv;
    return true;
}));


stage.show();

Clutter.main();
