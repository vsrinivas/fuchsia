import {NgModule} from '@angular/core';
import {MatButtonModule, MatCardModule, MatProgressSpinnerModule, MatToolbarModule} from '@angular/material';
import {BrowserModule} from '@angular/platform-browser';
import {AngularFireModule} from 'angularfire2';
import {AngularFireAuthModule} from 'angularfire2/auth';
import {AngularFirestoreModule} from 'angularfire2/firestore';

import {environment} from '../environments/environment';

import {AppComponent} from './app.component';

@NgModule({
  declarations: [AppComponent],
  imports: [
    BrowserModule,                                          //
    AngularFireModule.initializeApp(environment.firebase),  //
    AngularFirestoreModule,                                 //
    AngularFireAuthModule,                                  //
    MatButtonModule,                                        //
    MatCardModule,                                          //
    MatProgressSpinnerModule,                               //
    MatToolbarModule                                        //
  ],
  providers: [],
  bootstrap: [AppComponent]
})
export class AppModule {
}
