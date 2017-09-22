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
  delayBetweenEraseStagesMillis = 3000;
  authenticated = false;
  uid = null;
  version = 0;
  loading = true;
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
    let provider = new firebase.auth.GoogleAuthProvider();
    provider.setCustomParameters({
      prompt: 'select_account'
    });
    this.afAuth.auth.signInWithPopup(provider);
  }

  logout() {
    this.afAuth.auth.signOut();
  }

  rootPath() {
    return '/__default__V/' + this.uid;
  }

  versionPath() {
    return this.rootPath() + '/' + this.version;
  }

  userDevicesPath() {
    return this.versionPath() + '/__metadata/devices';
  }

  registerWatchers() {
    this.root = this.db.list(this.rootPath());
    this.root.subscribe((state) => {
      let next_version = 0;
      for (const version_object of state) {
        if (Number(version_object.$key) > Number(next_version)) {
          next_version = version_object.$key;
        }
      }
      this.version = next_version;
      this.devices = this.db.list(this.userDevicesPath());
      this.loading = false;
    });
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
