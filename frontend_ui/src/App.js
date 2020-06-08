import React, {Component} from 'react';

import AppBar from '@material-ui/core/AppBar';
import Grid from '@material-ui/core/Grid';
import Typography from '@material-ui/core/Typography';
import Toolbar from '@material-ui/core/Toolbar';
import Button from '@material-ui/core/Button';
import Table from '@material-ui/core/Table';
import TableBody from '@material-ui/core/TableBody';
import TableCell from '@material-ui/core/TableCell';
import TableContainer from '@material-ui/core/TableContainer';
import TableRow from '@material-ui/core/TableRow';
import Paper from '@material-ui/core/Paper';
import Switch from '@material-ui/core/Switch';

function formatTime(val) {
    var x = Number(val);
    return (x < 0.001)?((x*1000000).toPrecision(3) + " us"):((x*1000).toPrecision(3) + " ms");   
}

function formatPedestal(val) {
    var x = Number(val);
    if (x === 0) return "0";
    return x.toPrecision(5);
}

class App extends Component {
  state = {
     value: {state: "Not connected"},
     expertMode: false
  }

  updateREST() {
    fetch('http://' + window.location.hostname + ':5232/', {crossDomain:true})
    .then(res => res.json())
    .then(data => this.setState({ value: data }))
    .catch(error => {
        this.setState({value: {state: "Not connected"}})
     });
  }
  
  componentDidMount() { 
    this.updateREST();
    this.interval = setInterval(() => this.updateREST(), 5000);
  }

  componentWillUnmount() {
    clearInterval(this.interval);
  }

  reinitialize() {
     fetch('http://' + window.location.hostname + ':5232/detector/api/jf-0.1.0/command/initialize',{method : "PUT"});
  }


  handleChange = (event) => {
    this.setState({[event.target.name]: event.target.checked });
  };

  render() {
    return <div>
         <AppBar position="sticky">
       <Toolbar>
       <Typography variant="h6" style={{flexGrow: 0.5}}>
           JUNGFRAU 4M 
       </Typography>
       <Typography variant="h6" style={{flexGrow: 2.0}}>
           State: {this.state.value.state}
       </Typography>
       <Switch checked={this.state.expertMode} onChange={this.handleChange} name="expertMode" />
       <Typography variant="h6" style={{flexGrow: 0.2}}>
       Expert mode
       </Typography>
       <Button color="secondary" onClick={this.reinitialize} variant="contained" disableElevation>Initialize</Button>
       </Toolbar>
       </AppBar>
     <br/>
     <Typography variant="h6">

     </Typography><br/>
     <TableContainer component={Paper} style={{width: 650, marginLeft: "auto", marginRight: "auto"}}>
      <Table size="small"  aria-label="simple table">
          <TableBody>
            <TableRow>
              <TableCell component="th" scope="row"> Frame time </TableCell>
              <TableCell align="right">{formatTime(this.state.value.frame_time)}</TableCell>
            </TableRow>
            {this.state.expertMode?<TableRow>
              <TableCell component="th" scope="row"> Frame time (internal)</TableCell>
              <TableCell align="right">{formatTime(this.state.value.frame_time_detector)}</TableCell>
            </TableRow>:""}
            <TableRow>
              <TableCell component="th" scope="row"> Count time </TableCell>
              <TableCell align="right">{formatTime(this.state.value.count_time)}</TableCell>
            </TableRow>
            {this.state.expertMode?<TableRow>
              <TableCell component="th" scope="row"> Count time (internal)</TableCell>
              <TableCell align="right">{formatTime(this.state.value.count_time_detector)}</TableCell>
            </TableRow>:""}
            <TableRow>
              <TableCell component="th" scope="row"> Frame summation </TableCell>
              <TableCell align="right">{this.state.value.summation}</TableCell>
            </TableRow>
            <TableRow>
              <TableCell component="th" scope="row"> Compression </TableCell>
              <TableCell align="right">{this.state.value.compression}</TableCell>
            </TableRow>
          </TableBody>
      </Table>
    </TableContainer><br/><br/>
    <Grid container spacing={3}>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 6<br/>
              Bad pixels: {this.state.value.bad_pixels_mod6}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod6)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod6)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod6)}
          </Paper>
     </Grid>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 7<br/>
              Bad pixels: {this.state.value.bad_pixels_mod7}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod7)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod7)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod7)}
          </Paper>
     </Grid>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 4<br/>
              Bad pixels: {this.state.value.bad_pixels_mod4}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod4)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod4)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod4)}
          </Paper>
     </Grid>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 5<br/>
              Bad pixels: {this.state.value.bad_pixels_mod5}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod5)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod5)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod5)}
          </Paper>
     </Grid>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 2<br/>
              Bad pixels: {this.state.value.bad_pixels_mod2}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod2)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod2)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod2)}
          </Paper>
     </Grid>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 3<br/>
              Bad pixels: {this.state.value.bad_pixels_mod3}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod3)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod3)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod3)}
          </Paper>
     </Grid>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 0<br/>
              Bad pixels: {this.state.value.bad_pixels_mod0}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod0)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod0)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod0)}
          </Paper>
     </Grid>
     <Grid item xs={6}>
          <Paper style={{textAlign: 'center'}}>
              Module 1<br/>
              Bad pixels: {this.state.value.bad_pixels_mod1}<br/>
              Mean pedestal G0: {formatPedestal(this.state.value.pedestalG0_mean_mod1)}<br/>
              Mean pedestal G1: {formatPedestal(this.state.value.pedestalG1_mean_mod1)}<br/>
              Mean pedestal G2: {formatPedestal(this.state.value.pedestalG2_mean_mod1)}
          </Paper>
     </Grid>
     </Grid><br/>
    <iframe title="grafana" src="http://mx-jungfrau-1:3000/d-solo/npmZQ7kMk/servers?orgId=1&theme=light&panelId=12" width="100%" height="250" frameborder="0"/>

     </div>
  }
}

export default App;
