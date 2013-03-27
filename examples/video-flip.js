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

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const Clutter = imports.gi.Clutter;
const ClutterGst = imports.gi.ClutterGst;

const COLUMNS = 3;
const ROWS = 3;

if (ARGV.length < 2)
    throw "Need 2 arguments : video-wall.js videofile1 videofile2";

Clutter.init(null, null);
ClutterGst.init(null, null);


let stage = new Clutter.Stage();
stage.set_background_color(new Clutter.Color({ red: 0,
                                               green: 0,
                                               blue: 0,
                                               alpha: 0xff }));
stage.connect('destroy',
              Lang.bind(this, function() { Clutter.main_quit(); }));

player1 = new ClutterGst.Playback();
player1.set_filename(ARGV[0]);
player1.set_audio_volume(0);
player1.set_progress(0.20);

player2 = new ClutterGst.Playback();
player2.set_filename(ARGV[1]);
player2.set_audio_volume(0);
player2.set_progress(0.20);

let animateActor = function(actor, params) {
    let diffPropName = null;

    actor.save_easing_state();
    actor.set_easing_duration(params.duration);
    actor.set_easing_mode(params.mode);

    for (let p in params.properties) {
        let t = actor.get_transition(p);
        if (t != null && t.is_playing())
            return true;

        if (actor[p] != params.properties[p]) {
            actor[p] = params.properties[p];
            diffPropName = p;
        }
    }

    actor.restore_easing_state();

    if (diffPropName != null && params.onCompleted) {
        let transition = actor.get_transition(diffPropName);
        actor.connect('transition-stopped::' + diffPropName,
                      Lang.bind(params.scope, function() {
                          params.onCompleted(actor);
                      }));
    }

    return (diffPropName != null);
};

let actors = [];

let positionActor = function(actorId) {
    let actor = actors[actorId % actors.length];

    let stageWidth = stage.get_width();
    let stageHeight = stage.get_height();

    animateActor(actor,
                 { duration: 500,
                   mode: Clutter.AnimationMode.EASE_OUT_CUBIC,
                   properties: {
                       'x': actor._myXPos * actor.width + (stageWidth / 2) - (COLUMNS * actor.width) / 2,
                       'y': actor._myYPos * actor.height + (stageHeight / 2) - (ROWS * actor.height) / 2,
                   },
                 });
    animateActor(actor._backActor,
                 { duration: 500,
                   mode: Clutter.AnimationMode.EASE_OUT_CUBIC,
                   properties: {
                       'x': actor._myXPos * actor.width + (stageWidth / 2) - (COLUMNS * actor.width) / 2,
                       'y': actor._myYPos * actor.height + (stageHeight / 2) - (ROWS * actor.height) / 2,
                   },
                 });
};

let positionActors = function() {
    for (let i = 0; i < actors.length; i++)
        positionActor(i);
};

for (let i = 0; i < ROWS; i++) {
    for (let j = 0; j < COLUMNS; j++) {
        let input = new ClutterGst.Box({ x1: j / COLUMNS,
                                         x2: (j + 1) / COLUMNS,
                                         y1: i / ROWS,
                                         y2: (i + 1) / ROWS,
                                       })
        let actor =
            new ClutterGst.Crop({ reactive: true,
                                  cull_backface: true,
                                  pivot_point: new Clutter.Point({ x: 0.5,
                                                                   y: 0.5 }),
                                  width: 200,
                                  height: 200,
                                  x: -200,
                                  y: -200,
                                  input_region: input,
                                  player: player1,
                                });
        actor._backActor =
            new ClutterGst.Crop({ reactive: false,
                                  cull_backface: true,
                                  pivot_point: new Clutter.Point({ x: 0.5,
                                                                   y: 0.5 }),
                                  rotation_angle_y: 180,
                                  width: 200,
                                  height: 200,
                                  x: -200,
                                  y: -200,
                                  input_region: input,
                                  player: player2,
                                });
        stage.add_child(actor);
        stage.add_child(actor._backActor);

        actor._myXPos = j;
        actor._myYPos = i;
        actor._backActor._myXPos = j;
        actor._backActor._myYPos = i;

        actors.push(actor);

        let animEnterParams = {
            duration: 350,
            mode: Clutter.AnimationMode.EASE_OUT_CUBIC,
            properties: {
                'rotation-angle-y': 180,
            },
            scope: this,
            onCompleted: function(actor) {
                if (actor._nextParams) {
                    actor._inAnimation = animateActor(actor, actor._nextParams);
                    actor._nextParams = null;
                } else {
                    actor._inAnimation = false;
                }
            },
        };
        let animLeaveParams = {
            duration: 350,
            mode: Clutter.AnimationMode.EASE_OUT_CUBIC,
            properties: {
                'rotation-angle-y': 0,
            },
            scope: this,
            onCompleted: function(actor) {
                if (actor._nextParams) {
                    actor._inAnimation = animateActor(actor, actor._nextParams);
                    actor._nextParams = null;
                } else {
                    actor._inAnimation = false;
                }
            },
        };

        actor.connect('enter-event', Lang.bind(this, function(actor, event) {
            if (!actor._inAnimation) {
                actor._inAnimation = animateActor(actor, animEnterParams);
                actor._backActor._inAnimation = animateActor(actor._backActor, animLeaveParams);
            } else {
                actor._nextParams = animEnterParams;
                actor._backActor._nextParams = animLeaveParams;
            }
        }));
        actor.connect('leave-event', Lang.bind(this, function(actor, event) {
            if (!actor._inAnimation) {
                actor._inAnimation = animateActor(actor, animLeaveParams);
                actor._backActor._inAnimation = animateActor(actor._backActor, animEnterParams);
            } else {
                actor._nextParams = animLeaveParams;
                actor._backActor._nextParams = animEnterParams;
            }
        }));
    }
}

stage.connect('allocation-changed', Lang.bind(this, function() {
    positionActors();
}));
stage.set_user_resizable(true);

player1.set_playing(true);
player2.set_playing(true);

stage.show();

Clutter.main();
