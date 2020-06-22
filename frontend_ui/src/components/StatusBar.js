
import React, {Component} from 'react';

import AppBar from '@material-ui/core/AppBar';
import Switch from '@material-ui/core/Switch';
import Toolbar from '@material-ui/core/Toolbar';
import Typography from '@material-ui/core/Typography';
import Button from '@material-ui/core/Button';

class StatusBar extends Component {

  reinitialize() {
     fetch('http://' + window.location.hostname + '/jf/command/initialize',{method : "PUT"});
  }

  render() {
     return <AppBar position="sticky">
       <Toolbar>
       <Typography variant="h6" style={{flexGrow: 0.5}}>
           JUNGFRAU 4M
       </Typography>
       <Typography variant="h6" style={{flexGrow: 2.0}}>
           State: {this.props.daqState}
       </Typography>
       <Switch checked={this.props.expertMode} onChange={this.props.handleChange} name="expertMode" />
       <Typography variant="h6" style={{flexGrow: 0.2}}>
       Expert mode
       </Typography>
       <Button color="secondary" onClick={this.reinitialize} variant="contained" disableElevation>Initialize</Button>
       </Toolbar>
       </AppBar>
  }
}

export default StatusBar;
