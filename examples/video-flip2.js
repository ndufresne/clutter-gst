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

const COLUMNS = 5;
const ROWS = 4;
const TILE_WIDTH = 200;
const TILE_HEIGHT = 200;
const AUDIO_VOLUME_MAX = 0.30;

if (ARGV.length < 2)
    throw "Need at least 2 arguments : video-wall.js videofile1 videofile2 ...";

//

Players.prototype = {
    _init: function() {
        this._current = 1;
        this._currentFilename = 0;

        this._players = [];
        this._players[0] = new ClutterGst.Playback();
        this._players[0].set_audio_volume(0);
        this._players[1] = new ClutterGst.Playback();
        this._players[1].set_audio_volume(0);
        this._players[2] = new ClutterGst.Playback();
        this._players[2].set_audio_volume(0);
    },

    //

    switchFilenamesForward: function() {
        this._currentFilename += 1;
        this._currentFilename %= ARGV.length;
    },

    switchFilenamesBackward: function() {
        this._currentFilename += ARGV.length - 1;
        this._currentFilename %= ARGV.length;
    },

    getCurrentFilename: function() {
        return ARGV[this._currentFilename];
    },

    getNextFilename: function() {
        return ARGV[(this._currentFilename + 1) % ARGV.length];
    },

    getPreviousFilename: function() {
        return ARGV[(this._currentFilename + ARGV.length - 1) % ARGV.length];
    },

    //

    getCurrentPlayer: function() {
        return this._players[this._current];
    },

    getNextPlayer: function() {
        return this._players[(this._current + 1) % this._players.length];
    },

    getPreviousPlayer: function() {
        return this._players[(this._current + this._players.length - 1) %
                             this._players.length];
    },

    switchPlayersForward: function() {
        this.switchFilenamesForward();
        this.getPreviousPlayer().set_playing(false);
        this.getPreviousPlayer().set_filename(this.getNextFilename());
        this.getPreviousPlayer().set_playing(true);
        this.getPreviousPlayer().set_playing(false);
        this.getPreviousPlayer().set_audio_volume(0);
        this._current++;
        this._current %= this._players.length;
    },

    switchPlayersBackward: function() {
        this.switchFilenamesBackward();
        this.getNextPlayer().set_playing(false);
        this.getNextPlayer().set_filename(this.getPreviousFilename());
        this.getNextPlayer().set_playing(true);
        this.getPreviousPlayer().set_playing(false);
        this.getNextPlayer().set_audio_volume(0);
        this._current += this._players.length - 1;
        this._current %= this._players.length;
    },

    //

    updateActorsCurrent: function(actors, back) {
        for (let i in actors) {
            if (back)
                actors[i]._backActor.player = this.getCurrentPlayer();
            else
                actors[i].player = this.getCurrentPlayer();
        }
    },

    updateActorsNext: function(actors, back) {
        for (let i in actors) {
            if (back)
                actors[i]._backActor.player = this.getNextPlayer();
            else
                actors[i].player = this.getNextPlayer();
        }
    },

    updateActorsPrevious: function(actors, back) {
        for (let i in actors) {
            if (back)
                actors[i]._backActor.player = this.getPreviousPlayer();
            else
                actors[i].player = this.getPreviousPlayer();
        }
    },

    dumpFilenames: function() {
        log(this.getPreviousPlayer().uri + "/" + this.getPreviousPlayer().get_playing());
        log(this.getCurrentPlayer().uri + "/" + this.getCurrentPlayer().get_playing());
        log(this.getNextPlayer().uri + "/" + this.getNextPlayer().get_playing());
        log("---------------");
    },
};

function Players() {
    this._init();
}

//

Clutter.init(null, null);
ClutterGst.init(null, null);

let animateActor = function(actor, params) {
    let diffPropName = null;

    actor.save_easing_state();
    if (params.duration)
        actor.set_easing_duration(params.duration);
    if (params.mode)
        actor.set_easing_mode(params.mode);
    if (params.delay)
        actor.set_easing_delay(params.delay);

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

//

let stage = new Clutter.Stage({ width: TILE_WIDTH * COLUMNS,
                                height: TILE_HEIGHT * ROWS,
                              });
log(stage.width + "x" + stage.height);
stage.set_background_color(new Clutter.Color({ red: 0,
                                               green: 0,
                                               blue: 0,
                                               alpha: 0xff }));
stage.set_user_resizable(true);
stage.connect('destroy',
              Lang.bind(this, function() { Clutter.main_quit(); }));

let players = new Players();
players.switchPlayersForward();
players.switchPlayersForward();
players.switchPlayersForward();
players.switchPlayersBackward();


let actors = [];

let positionActor = function(actorId) {
    let actor = actors[actorId % actors.length];

    let stageWidth = stage.get_width();
    let stageHeight = stage.get_height();

    actor._backActor.x = actor.x = actor._myXPos * actor.width + (stageWidth / 2) - (COLUMNS * actor.width) / 2;
    actor._backActor.y = actor.y = actor._myYPos * actor.height + (stageHeight / 2) - (ROWS * actor.height) / 2;
};

let positionActors = function() {
    for (let i = 0; i < actors.length; i++)
        positionActor(i);
};

for (let i = 0; i < ROWS; i++) {
    for (let j = 0; j < COLUMNS; j++) {
        let input = new Clutter.ActorBox({ x1: j / COLUMNS,
                                           x2: (j + 1) / COLUMNS,
                                           y1: i / ROWS,
                                           y2: (i + 1) / ROWS,
                                         })
        let actor =
            new ClutterGst.Crop({ cull_backface: true,
                                  pivot_point: new Clutter.Point({ x: 0.5,
                                                                   y: 0.5 }),
                                  width: TILE_WIDTH,
                                  height: TILE_HEIGHT,
                                  input_region: input,
                                });
        actor._backActor =
            new ClutterGst.Crop({ cull_backface: true,
                                  pivot_point: new Clutter.Point({ x: 0.5,
                                                                   y: 0.5 }),
                                  rotation_angle_y: 180,
                                  width: TILE_WIDTH,
                                  height: TILE_HEIGHT,
                                  input_region: input,
                                });
        stage.add_child(actor);
        stage.add_child(actor._backActor);

        actor._myXPos = j;
        actor._myYPos = i;
        actor._backActor._myXPos = j;
        actor._backActor._myYPos = i;

        actors.push(actor);
    }
}

players.updateActorsCurrent(actors, false);
players.getCurrentPlayer().set_audio_volume(AUDIO_VOLUME_MAX);
players.getCurrentPlayer().set_playing(true);

//
//
//

let animateSound = function(duration, mode, newPlayer, oldPlayer) {
    let tl = new Clutter.Timeline({
        duration: duration,
        progress_mode: mode,
    });

    let oldLevel = oldPlayer.get_audio_volume();

    tl.connect('new-frame', Lang.bind(this, function() {
        newPlayer.set_audio_volume(oldLevel * tl.get_progress());
        oldPlayer.set_audio_volume(oldLevel * (1 - tl.get_progress()))
    }));
    tl.start();
};

let animateForward = function(duration, mode, delay) {
    for (let i in actors) {
        let actor = actors[i];
        let actorDelay = delay * (((actor._myXPos  + actor._myYPos * COLUMNS / ROWS) / (2 * COLUMNS)));
        let props = { duration: duration,
                      mode: mode,
                      delay: actorDelay,
                      properties: {
                          'rotation-angle-y': actor.rotation_angle_y + 180,
                      },
                    };

        animateActor(actor, props);
        props.properties['rotation-angle-y'] = actor._backActor.rotation_angle_y + 180;
        animateActor(actor._backActor, props);
    }
};

let animateBackward = function(duration, mode, delay) {
    for (let i in actors) {
        let actor = actors[i];
        let actorDelay = delay * (1 - ((actor._myXPos  + actor._myYPos * COLUMNS / ROWS) / (2 * COLUMNS)));
        let props = { duration: duration,
                      mode: mode,
                      delay: actorDelay,
                      properties: {
                          'rotation-angle-y': actor.rotation_angle_y - 180,
                      },
                    };

        animateActor(actor, props);
        props.properties['rotation-angle-y'] = actor._backActor.rotation_angle_y - 180;
        animateActor(actor._backActor, props);
    }
};

stage.connect('allocation-changed', Lang.bind(this, function() {
    positionActors();
}));

let inAnimation = false;
let nextSide = true;
stage.connect('key-press-event', Lang.bind(this, function(actor, event) {
    if (inAnimation)
        return false;

    log('event processing!');
    let currentPlayer = players.getCurrentPlayer();
    let nextPlayer;

    //players.dumpFilenames();

    switch (event.get_key_symbol()) {
    default:
        return false;

    case Clutter.Up:
        players.updateActorsNext(actors, nextSide);
        animateForward(400, Clutter.AnimationMode.EASE_IN_OUT_CUBIC, 1000);
        nextPlayer = players.getNextPlayer();
        players.switchPlayersForward();
        break;

    case Clutter.Down:
        players.updateActorsPrevious(actors, nextSide);
        animateBackward(400, Clutter.AnimationMode.EASE_IN_OUT_CUBIC, 1000);
        nextPlayer = players.getPreviousPlayer();
        players.switchPlayersBackward();
        break;
    }

    inAnimation = true;
    nextSide = !nextSide;

    animateSound(400 + 1000, Clutter.AnimationMode.EASE_IN_OUT_CUBIC,
                 nextPlayer, currentPlayer);
    nextPlayer.set_playing(true);


    Mainloop.timeout_add(400 + 1000 + 50, Lang.bind(this, function() {
        players.getPreviousPlayer().set_playing(false);
        //players.dumpFilenames();
        inAnimation = false;
        log('event ended');
    }));

    return true;
}));

stage.show();

Clutter.main();
