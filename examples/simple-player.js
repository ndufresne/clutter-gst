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
const Clutter = imports.gi.Clutter;
const ClutterGst = imports.gi.ClutterGst;

if (ARGV.length < 1)
    throw "Need 1 argument : simple-player.js videofile ";

ClutterGst.init(null, null);

let stage = new Clutter.Stage({
    width: 800,
    height: 600,
    layout_manager: new Clutter.BinLayout({
        x_align: Clutter.BinAlignment.FILL,
        y_align: Clutter.BinAlignment.FILL,
    }),
});
stage.connect('destroy',
              Lang.bind(this, function() { Clutter.main_quit(); }));

let player = new ClutterGst.Playback();
player.set_filename(ARGV[0]);
player.set_audio_volume(0.75);
player.set_playing(true);

let actor = new Clutter.Actor({
    content: new ClutterGst.Aspectratio({
        player: player,
        paint_borders: true,
    }),
});
stage.add_child(actor);

stage.show();

Clutter.main();
