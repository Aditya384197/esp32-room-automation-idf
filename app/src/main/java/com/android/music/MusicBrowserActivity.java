/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.music;

import android.Manifest;
import android.app.Activity;
import android.content.ContentUris;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

/**
 * Minimal launcher/library screen added for the standalone Android 10 build.
 * The upstream Android 16 repository has this activity as an empty stub.
 */
public class MusicBrowserActivity extends Activity {
    private static final int REQUEST_READ_AUDIO = 1001;
    private final List<Song> songs = new ArrayList<>();
    private SongAdapter adapter;
    private TextView emptyView;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        createContentView();
        if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE},
                    REQUEST_READ_AUDIO);
        } else {
            loadSongs();
        }
    }

    private void createContentView() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFFF5F5F5);

        TextView heading = new TextView(this);
        heading.setText(R.string.app_name);
        heading.setTextColor(0xFF202124);
        heading.setTextSize(24);
        heading.setGravity(Gravity.CENTER_VERTICAL);
        heading.setPadding(dp(20), dp(22), dp(20), dp(12));
        heading.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(heading, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView subheading = new TextView(this);
        subheading.setText("Songs on this device");
        subheading.setTextColor(0xFF5F6368);
        subheading.setTextSize(14);
        subheading.setPadding(dp(20), 0, dp(20), dp(12));
        root.addView(subheading, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ListView listView = new ListView(this);
        listView.setDividerHeight(dp(1));
        listView.setBackgroundColor(0xFFFFFFFF);
        emptyView = new TextView(this);
        emptyView.setText("No music found on this device.");
        emptyView.setTextColor(0xFF5F6368);
        emptyView.setGravity(Gravity.CENTER);
        emptyView.setPadding(dp(24), dp(24), dp(24), dp(24));
        root.addView(listView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        root.addView(emptyView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        adapter = new SongAdapter();
        listView.setAdapter(adapter);
        listView.setEmptyView(emptyView);
        listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                Song song = adapter.getItem(position);
                if (song == null) return;
                Intent play = new Intent(MusicBrowserActivity.this, AudioPreview.class);
                play.setAction(Intent.ACTION_VIEW);
                play.setDataAndType(song.uri, "audio/*");
                startActivity(play);
            }
        });
        setContentView(root);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void loadSongs() {
        songs.clear();
        Uri collection = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI;
        String[] projection = {
                MediaStore.Audio.Media._ID,
                MediaStore.Audio.Media.TITLE,
                MediaStore.Audio.Media.ARTIST
        };
        String selection = MediaStore.Audio.Media.IS_MUSIC + "!=0";
        String sortOrder = MediaStore.Audio.Media.TITLE + " COLLATE NOCASE ASC";
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(collection, projection, selection,
                    null, sortOrder);
            if (cursor != null) {
                int idColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media._ID);
                int titleColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE);
                int artistColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST);
                while (cursor.moveToNext()) {
                    long id = cursor.getLong(idColumn);
                    String title = cursor.getString(titleColumn);
                    String artist = cursor.getString(artistColumn);
                    Uri itemUri = ContentUris.withAppendedId(collection, id);
                    songs.add(new Song(itemUri,
                            title == null || title.length() == 0 ? "Unknown title" : title,
                            artist == null || artist.length() == 0 ? "Unknown artist" : artist));
                }
            }
        } catch (SecurityException | IllegalArgumentException exception) {
            Toast.makeText(this, "Unable to read device music.", Toast.LENGTH_LONG).show();
        } finally {
            if (cursor != null) cursor.close();
        }
        adapter.clear();
        adapter.addAll(songs);
        adapter.notifyDataSetChanged();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_READ_AUDIO) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                loadSongs();
            } else {
                Toast.makeText(this,
                        "Storage permission is needed to list local music.",
                        Toast.LENGTH_LONG).show();
            }
        }
    }

    private static final class Song {
        final Uri uri;
        final String title;
        final String artist;

        Song(Uri uri, String title, String artist) {
            this.uri = uri;
            this.title = title;
            this.artist = artist;
        }
    }

    private final class SongAdapter extends ArrayAdapter<Song> {
        SongAdapter() {
            super(MusicBrowserActivity.this, android.R.layout.simple_list_item_2,
                    android.R.id.text1, new ArrayList<Song>());
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            View row = super.getView(position, convertView, parent);
            Song item = getItem(position);
            TextView title = row.findViewById(android.R.id.text1);
            TextView artist = row.findViewById(android.R.id.text2);
            if (item != null) {
                title.setText(item.title);
                title.setTextColor(0xFF202124);
                title.setTextSize(16);
                artist.setText(item.artist);
                artist.setTextColor(0xFF6B7075);
                artist.setTextSize(13);
            }
            row.setPadding(dp(16), dp(5), dp(16), dp(5));
            return row;
        }
    }
}
