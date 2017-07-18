import { Component } from '@angular/core';
import { AngularFireDatabase, FirebaseListObservable } from 'angularfire2/database';
import { AngularFireAuth } from 'angularfire2/auth';
import { Observable } from 'rxjs/Observable';
import * as firebase from 'firebase/app';

@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.css']
})
export class AppComponent {
  title = 'Ledger Dashboard';
  delayBetweenEraseStagesMillis = 3000
  authenticated = false;
  uid = null;
  version = 15;
  deleteInProgress = false;

  user: Observable<firebase.User>;
  devices: FirebaseListObservable<any[]>;
  root: FirebaseListObservable<any[]>;

  constructor(public afAuth: AngularFireAuth, private db: AngularFireDatabase) {
    this.user = afAuth.authState;

    this.afAuth.authState.subscribe((state) => {
      this.authenticated = state != null;
      if (this.authenticated) {
        this.uid = state.uid;
        this.registerWatchers();
      } else {
        this.unregisterWatchers();
      }
    });
  }

  login() {
    this.afAuth.auth.signInWithPopup(new firebase.auth.GoogleAuthProvider());
  }

  logout() {
    this.afAuth.auth.signOut();
  }

  userPath() {
    return '/__default__V/' + this.uid + '/' + String(this.version);
  }

  userDevicesPath() {
    return this.userPath() + '/__metadata/devices';
  }

  registerWatchers() {
    this.devices = this.db.list(this.userDevicesPath());
    this.root = this.db.list(this.userPath());

  }

  unregisterWatchers() {
    this.devices = null;
    this.root = null;
  }

  erase() {
    this.deleteInProgress = true;
    this.devices.remove();
    setTimeout(() => {
      this.root.remove();
      this.deleteInProgress = false;
    }, this.delayBetweenEraseStagesMillis);
  }
}
