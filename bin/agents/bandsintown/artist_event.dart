// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// https://www.bandsintown.com/api/responses#event-json
class ArtistEvent {
  final String url;
  final String ticketUrl;
  final int id;
  final String ticketStatus;
  final DateTime datetime;
  final Venue venue;
  final List<Artist> artists;

  ArtistEvent(
      {this.url,
      this.ticketUrl,
      this.id,
      this.ticketStatus,
      this.datetime,
      this.venue,
      this.artists});

  // TODO(rosswang): Use serialization library? (Would that require mirrors?)
  factory ArtistEvent.fromJson(Map<String, dynamic> json) => new ArtistEvent(
      url: json["url"],
      ticketUrl: json["ticket_url"],
      id: json["id"],
      ticketStatus: json["ticket_status"],
      datetime: DateTime.parse(json["datetime"]),
      venue: new Venue.fromJson(json["venue"]),
      artists: json["artists"]
          .map((artist) => new Artist.fromJson(artist))
          .toList());
}

class Venue {
  final String name;
  final String url;
  final int id;
  final String city;
  final String region;
  final String country;
  final double latitude;
  final double longitude;

  Venue(
      {this.name,
      this.url,
      this.id,
      this.city,
      this.region,
      this.country,
      this.latitude,
      this.longitude});

  factory Venue.fromJson(Map<String, dynamic> json) => new Venue(
      name: json["name"],
      id: json["id"],
      url: json["url"],
      city: json["city"],
      region: json["region"],
      country: json["country"],
      latitude: json["latitude"],
      longitude: json["longitude"]);
}

class Artist {
  final String name;
  final String musicBrainzId;
  final String url;

  Artist({this.name, this.musicBrainzId, this.url});

  factory Artist.fromJson(Map<String, dynamic> json) => new Artist(
      name: json["name"], musicBrainzId: json["mbid"], url: json["url"]);
}
