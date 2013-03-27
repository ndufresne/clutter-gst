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

const BIT_WIDTH = 200;
const BIT_HEIGHT = 200;

if (ARGV.length < 1)
    throw "Needs 1 arguments : piece.js videofile";

//

Pieces.prototype = {
    _init: function(rows, columns) {
        this.rows = rows;
        this.columns = columns;

        this.array = [];
        for (let i = 0; i < rows; i++)
            this.array[i] = [];

        this.gap = { x: columns - 1,
                     y: rows - 1 };
    },

    _moveToGap: function(fromX, fromY) {
        let actor = this.array[fromY][fromX];

        this.array[fromY][fromX] = undefined;

        actor.save_easing_state();
        actor.set_easing_duration(300);
        actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_CUBIC);

        actor.set_position(this.gap.x * actor.width,
                           this.gap.y * actor.height);

        actor.restore_easing_state();

        this.array[this.gap.y][this.gap.x] = actor;
    },

    moveLeft: function() {
        if (this.gap.x >= this.columns - 1)
            return;

        this._moveToGap(this.gap.x + 1, this.gap.y);
        this.gap.x += 1;
    },

    moveRight: function() {
        if (this.gap.x <= 0)
            return;

        this._moveToGap(this.gap.x - 1, this.gap.y);
        this.gap.x -= 1;
    },

    moveUp: function() {
        if (this.gap.y >= this.rows - 1)
            return;

        this._moveToGap(this.gap.x, this.gap.y + 1);
        this.gap.y += 1;
    },

    moveDown: function() {
        if (this.gap.y <= 0)
            return;

        this._moveToGap(this.gap.x, this.gap.y - 1);
        this.gap.y -= 1;
    },

};

function Pieces(rows, columns) {
    this._init(rows, columns);
}

//

Clutter.init(null, null);
ClutterGst.init(null, null);

let stage = new Clutter.Stage();
stage.set_size(BIT_WIDTH * 3, BIT_HEIGHT * 3);
stage.set_user_resizable(true);

let player = new ClutterGst.Playback();
player.set_filename(ARGV[0]);
player.set_audio_volume(0.25);
//let player = new ClutterGst.Camera();

let pieces = new Pieces(ROWS, COLUMNS);
for (let i = 0; i < ROWS; i++) {
    for (let j = 0; j < COLUMNS; j++) {
        if ((i * ROWS + j) >= (ROWS * COLUMNS - 1))
            break;

        let input = new ClutterGst.Box({ x1: j / COLUMNS,
                                         x2: (j + 1) / COLUMNS,
                                         y1: i / ROWS,
                                         y2: (i + 1) / ROWS,
                                       })
        let actor = new ClutterGst.Crop({ width: BIT_WIDTH,
                                          height: BIT_HEIGHT,
                                          player: player,
                                          x: BIT_WIDTH * j,
                                          y: BIT_HEIGHT * i,
                                          input_region: input,
                                        });
        stage.add_actor(actor);

        pieces.array[i][j] = actor;
    }
}

player.set_playing(true);

stage.connect('key-press-event', Lang.bind(this, function(actor, event) {
    switch (event.get_key_symbol()) {
    case Clutter.Left:
        pieces.moveLeft();
        break;

    case Clutter.Right:
        pieces.moveRight();
        break;

    case Clutter.Up:
        pieces.moveUp();
        break;

    case Clutter.Down:
        pieces.moveDown();
        break;

    default:
        log(event.get_key_symbol());
        break;
    }
}));

stage.show();
stage.connect('destroy',
              Lang.bind(this, function() { Clutter.main_quit(); }));

Clutter.main();
